#include "demi/runtime/physics/Box2DShapes2D.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/PhysicsGeometry2D.h"
#include "demi/runtime/physics/RadialRaycast2D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/WorldQueries.h"

#include <box2d/b2_distance.h>
#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <variant>

namespace demi::runtime {
namespace {
using NativeShape = std::variant<b2CircleShape, b2PolygonShape, b2EdgeShape>;

struct QueryShape {
  NativeShape shape;
  b2Transform transform;
  std::string entityId;
  std::string layer;
  bool isTrigger;
};

const b2Shape &nativeShape(const QueryShape &shape) {
  return std::visit([](const auto &value) -> const b2Shape & { return value; },
                    shape.shape);
}

b2DistanceOutput shapeDistance(const b2Shape &left,
                               const b2Transform &leftTransform,
                               const b2Shape &right,
                               const b2Transform &rightTransform) {
  b2DistanceInput input;
  input.proxyA.Set(&left, 0);
  input.proxyB.Set(&right, 0);
  input.transformA = leftTransform;
  input.transformB = rightTransform;
  input.useRadii = true;
  b2SimplexCache cache{};
  b2DistanceOutput output;
  b2Distance(&output, &cache, &input);
  return output;
}

b2Transform at(Vec2 position) {
  return b2Transform({position.x, position.y}, b2Rot(0));
}

b2CircleShape circleShape(float radius) {
  b2CircleShape shape;
  shape.m_radius = std::max(radius, 0.0F);
  return shape;
}

b2AABB radialBounds(Vec2 origin, float radius) {
  b2AABB bounds;
  circleShape(radius).ComputeAABB(&bounds, at(origin), 0);
  return bounds;
}

b2PolygonShape boxShape(Vec2 size) {
  b2PolygonShape shape;
  shape.SetAsBox(std::max(size.x * 0.5F, b2_epsilon),
                 std::max(size.y * 0.5F, b2_epsilon));
  return shape;
}
} // namespace

// An operation-scoped snapshot owns native shapes, not a second simulation
// world or a cache that can become stale after direct authored component edits.
class NativeQueryShapes2D {
public:
  NativeQueryShapes2D(const World &world, const std::string &layer,
                      const std::string &ignoredEntityId,
                      std::optional<b2AABB> bounds = std::nullopt) {
    for (const auto index : physics2d_detail::physicsEntityIndices2D(world)) {
      const Entity &entity = world.entities[index];
      if (!entity.enabled || entity.id == ignoredEntityId ||
          !entity.hasComponent<Transform2DComponent>() ||
          !physics2d_detail::hasCollider(entity)) {
        continue;
      }
      const std::string entityLayer = physics2d_detail::colliderLayer(entity);
      if (!layer.empty() && entityLayer != layer) {
        continue;
      }
      Transform2DComponent transform;
      const auto pose = resolveWorldTransform2D(world, entity);
      if (!pose) {
        continue;
      }
      transform.position = pose->position;
      transform.rotation = pose->rotation;
      transform.scale = pose->scale;
      const b2Transform nativeTransform(
          {transform.position.x, transform.position.y},
          b2Rot(transform.rotation));
      const auto collectFixture = [&](const b2FixtureDef &fixture) {
        const auto append = [&](NativeShape shape) {
          if (bounds) {
            b2AABB shapeBounds;
            std::visit(
                [&](const auto &value) {
                  value.ComputeAABB(&shapeBounds, nativeTransform, 0);
                },
                shape);
            if (!b2TestOverlap(*bounds, shapeBounds)) {
              return;
            }
          }
          shapes.push_back({std::move(shape), nativeTransform, entity.id,
                            entityLayer, fixture.isSensor});
        };
        switch (fixture.shape->GetType()) {
        case b2Shape::e_circle:
          append(*static_cast<const b2CircleShape *>(fixture.shape));
          break;
        case b2Shape::e_polygon:
          append(*static_cast<const b2PolygonShape *>(fixture.shape));
          break;
        case b2Shape::e_edge:
          append(*static_cast<const b2EdgeShape *>(fixture.shape));
          break;
        case b2Shape::e_chain: {
          const auto &chain = *static_cast<const b2ChainShape *>(fixture.shape);
          for (int32 child = 0; child < chain.GetChildCount(); ++child) {
            b2EdgeShape edge;
            chain.GetChildEdge(&edge, child);
            append(edge);
          }
          break;
        }
        default:
          break;
        }
      };
      // Keep the erased callback to one pointer so collecting each entity does
      // not allocate a std::function target during visibility fan construction.
      visitBox2DFixtures2D(world, entity, transform,
                           [&collectFixture](const b2FixtureDef &fixture) {
                             collectFixture(fixture);
                           });
    }
  }

  std::vector<QueryShape> shapes;
};

namespace {
std::vector<PhysicsQueryHit2D>
overlapShapes(const NativeQueryShapes2D &candidates, const b2Shape &query,
              Vec2 center, float tolerance = 0.0F,
              bool includeTriggers = true) {
  std::vector<PhysicsQueryHit2D> hits;
  const b2Transform queryTransform = at(center);
  const b2CircleShape point = circleShape(0);
  for (const QueryShape &candidate : candidates.shapes) {
    if (!includeTriggers && candidate.isTrigger) {
      continue;
    }
    const b2Shape &shape = nativeShape(candidate);
    const auto separation =
        shapeDistance(query, queryTransform, shape, candidate.transform);
    if (separation.distance > tolerance + b2_epsilon) {
      continue;
    }
    const auto closest =
        shapeDistance(point, queryTransform, shape, candidate.transform);
    const b2Vec2 delta = closest.pointA - closest.pointB;
    const float distance = delta.Length();
    const b2Vec2 normal =
        distance > b2_epsilon ? (1.0F / distance) * delta : b2Vec2_zero;
    hits.push_back({.entityId = candidate.entityId,
                    .layer = candidate.layer,
                    .point = {closest.pointB.x, closest.pointB.y},
                    .normal = {normal.x, normal.y},
                    .distance = distance});
  }
  std::ranges::sort(hits, [](const auto &left, const auto &right) {
    return left.entityId == right.entityId ? left.distance < right.distance
                                           : left.entityId < right.entityId;
  });
  const auto duplicates =
      std::ranges::unique(hits, {}, &PhysicsQueryHit2D::entityId);
  hits.erase(duplicates.begin(), duplicates.end());
  return hits;
}

std::optional<PhysicsRaycastHit2D>
raycastShapes(const NativeQueryShapes2D &candidates, Vec2 origin,
              Vec2 direction, float distance) {
  const float length = std::hypot(direction.x, direction.y);
  if (length <= 0.000001F || distance <= 0.0F) {
    return std::nullopt;
  }
  const b2RayCastInput input{{origin.x, origin.y},
                             {origin.x + direction.x / length * distance,
                              origin.y + direction.y / length * distance},
                             1.0F};
  std::optional<PhysicsRaycastHit2D> closest;
  for (const QueryShape &candidate : candidates.shapes) {
    const b2Shape &shape = nativeShape(candidate);
    b2RayCastOutput output;
    output.fraction = 0.0F;
    output.normal.SetZero();
    const bool containsOrigin = shape.TestPoint(candidate.transform, input.p1);
    if (!containsOrigin &&
        !shape.RayCast(&output, input, candidate.transform, 0)) {
      continue;
    }
    if (closest && (output.fraction > closest->fraction ||
                    (output.fraction == closest->fraction &&
                     candidate.entityId >= closest->entityId))) {
      continue;
    }
    const b2Vec2 point = input.p1 + output.fraction * (input.p2 - input.p1);
    closest = PhysicsRaycastHit2D{.entityId = candidate.entityId,
                                  .layer = candidate.layer,
                                  .point = {point.x, point.y},
                                  .normal = {output.normal.x, output.normal.y},
                                  .distance = output.fraction * distance,
                                  .fraction = output.fraction};
  }
  return closest;
}
} // namespace

bool overlapBox(const World &world, Vec2 center, Vec2 size,
                const std::string &ignoredEntityId) {
  // Ground probes intentionally tolerate the small resting separation of the
  // solver.
  return !overlapShapes(NativeQueryShapes2D(world, {}, ignoredEntityId),
                        boxShape(size), center, 0.06F, false)
              .empty();
}

std::vector<PhysicsQueryHit2D>
overlapBoxAll(const World &world, Vec2 center, Vec2 size,
              const std::string &layer, const std::string &ignoredEntityId) {
  return overlapShapes(NativeQueryShapes2D(world, layer, ignoredEntityId),
                       boxShape(size), center, 0.06F);
}

std::vector<PhysicsQueryHit2D>
overlapCircleAll(const World &world, Vec2 center, float radius,
                 const std::string &layer, const std::string &ignoredEntityId) {
  auto hits = overlapShapes(NativeQueryShapes2D(world, layer, ignoredEntityId),
                            circleShape(radius), center);
  for (auto &hit : hits) {
    hit.fraction = radius > 0.0F ? hit.distance / radius : 0.0F;
  }
  return hits;
}

std::vector<std::string> overlapCircle(const World &world, Vec2 center,
                                       float radius, const std::string &layer,
                                       const std::string &ignoredEntityId) {
  std::vector<std::string> result;
  for (const auto &hit :
       overlapCircleAll(world, center, radius, layer, ignoredEntityId)) {
    result.push_back(hit.entityId);
  }
  return result;
}

std::optional<PhysicsRaycastHit2D>
raycast2D(const World &world, Vec2 origin, Vec2 direction, float distance,
          const std::string &layer, const std::string &ignoredEntityId) {
  return raycastShapes(NativeQueryShapes2D(world, layer, ignoredEntityId),
                       origin, direction, distance);
}

RadialRaycast2D::RadialRaycast2D(const World &world, Vec2 origin, float radius,
                                 const std::string &layer,
                                 const std::string &ignoredEntityId)
    : origin_(origin), radius_(radius),
      candidates_(std::make_unique<NativeQueryShapes2D>(
          world, layer, ignoredEntityId, radialBounds(origin, radius))) {
  const b2CircleShape query = circleShape(radius);
  std::erase_if(candidates_->shapes, [&](const QueryShape &candidate) {
    return !b2TestOverlap(&query, 0, &nativeShape(candidate), 0, at(origin),
                          candidate.transform);
  });
}

RadialRaycast2D::~RadialRaycast2D() = default;

std::vector<float> RadialRaycast2D::silhouetteAngles() const {
  constexpr float tau = 2 * std::numbers::pi_v<float>;
  std::vector<float> angles;
  const auto appendAngle = [&](float angle) {
    angles.push_back(std::fmod(angle + tau, tau));
  };
  const auto appendPoint = [&](b2Vec2 point) {
    const b2Vec2 relative = point - b2Vec2(origin_.x, origin_.y);
    if (relative.LengthSquared() > 0.0F) {
      float angle = std::atan2(relative.y, relative.x);
      if (angle < 0.0F) {
        angle += tau;
      }
      if (angle >= tau) {
        angle = 0.0F;
      }
      angles.push_back(angle);
    }
  };
  for (const QueryShape &candidate : candidates_->shapes) {
    std::visit(
        [&](const auto &shape) {
          using Shape = std::decay_t<decltype(shape)>;
          if constexpr (std::is_same_v<Shape, b2CircleShape>) {
            const b2Vec2 center = b2Mul(candidate.transform, shape.m_p);
            const b2Vec2 relative = center - b2Vec2(origin_.x, origin_.y);
            const float distance = relative.Length();
            if (distance > shape.m_radius && shape.m_radius > 0.0F) {
              const float angle = std::atan2(relative.y, relative.x);
              const float tangent = std::asin(shape.m_radius / distance);
              appendAngle(angle - tangent);
              appendAngle(angle + tangent);
            }
          } else if constexpr (std::is_same_v<Shape, b2PolygonShape>) {
            for (int32 index = 0; index < shape.m_count; ++index) {
              appendPoint(b2Mul(candidate.transform, shape.m_vertices[index]));
            }
          } else {
            appendPoint(b2Mul(candidate.transform, shape.m_vertex1));
            appendPoint(b2Mul(candidate.transform, shape.m_vertex2));
          }
        },
        candidate.shape);
  }
  std::ranges::sort(angles);
  const auto duplicates = std::ranges::unique(angles);
  angles.erase(duplicates.begin(), duplicates.end());
  return angles;
}

std::optional<PhysicsRaycastHit2D> RadialRaycast2D::cast(Vec2 direction) const {
  return cast(direction, radius_);
}

std::optional<PhysicsRaycastHit2D> RadialRaycast2D::cast(Vec2 direction,
                                                         float distance) const {
  return raycastShapes(*candidates_, origin_, direction,
                       std::min(distance, radius_));
}
PhysicsRaycastBatch2DResult raycastBatch2D(const World &world, Vec2 origin,
                                           std::span<const PhysicsRay2D> rays,
                                           const std::string &layer,
                                           const std::string &ignoredEntityId) {
  ProfileScope batchScope("Physics2D.raycast_batch");
  if (!std::isfinite(origin.x) || !std::isfinite(origin.y) ||
      rays.size() > 4096) {
    return {{}, "Ray batch requires a finite origin and at most 4096 rays."};
  }
  float radius = 0;
  for (const PhysicsRay2D &ray : rays) {
    if (!std::isfinite(ray.direction.x) || !std::isfinite(ray.direction.y) ||
        !std::isfinite(ray.distance) || ray.distance < 0) {
      return {{},
              "Ray directions must be finite and distances finite and "
              "non-negative."};
    }
    radius = std::max(radius, ray.distance);
  }
  PhysicsRaycastBatch2DResult result;
  result.hits.reserve(rays.size());
  if (rays.empty()) {
    return result;
  }
  const RadialRaycast2D query(world, origin, radius, layer, ignoredEntityId);
  for (const PhysicsRay2D &ray : rays) {
    result.hits.push_back(query.cast(ray.direction, ray.distance));
  }
  return result;
}

std::vector<PhysicsContact2D> contactsForEntity(const World &world,
                                                const std::string &entityId) {
  std::vector<PhysicsContact2D> contacts;
  for (const PhysicsContact2D &contact : world.physicsContacts) {
    if (contact.entityId == entityId) {
      contacts.push_back(contact);
    }
  }
  return contacts;
}

bool hasContact(const World &world, const std::string &entityId,
                const PhysicsContactFilter2D &filter) {
  for (const PhysicsContact2D &contact : world.physicsContacts) {
    if (contact.entityId != entityId) {
      continue;
    }
    if (contact.phase == "exit") {
      continue;
    }
    if (!filter.includeTriggers && contact.isTrigger) {
      continue;
    }
    if (filter.layer.has_value() && contact.otherLayer != *filter.layer) {
      continue;
    }
    if (filter.normalXMin.has_value() &&
        contact.normal.x < *filter.normalXMin) {
      continue;
    }
    if (filter.normalXMax.has_value() &&
        contact.normal.x > *filter.normalXMax) {
      continue;
    }
    if (filter.normalYMin.has_value() &&
        contact.normal.y < *filter.normalYMin) {
      continue;
    }
    if (filter.normalYMax.has_value() &&
        contact.normal.y > *filter.normalYMax) {
      continue;
    }
    return true;
  }
  return false;
}

} // namespace demi::runtime
