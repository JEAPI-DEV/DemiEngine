#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/RadialRaycast2D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include <ranges>
#include "demi/runtime/physics/PhysicsGeometry2D.h"
#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/CircleCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace demi::runtime {

using physics2d_detail::Aabb;
using physics2d_detail::absoluteScale;
using physics2d_detail::circleScale;
using physics2d_detail::colliderAabb;
using physics2d_detail::colliderLayer;
using physics2d_detail::hasCollider;
using physics2d_detail::makeAabb;
using physics2d_detail::participatesInCollision;
using physics2d_detail::queryIntersects;
using physics2d_detail::scaledLocalPoint;

bool overlapBox(const World &world, const Vec2 center, const Vec2 size,
                const std::string &ignoredEntityId) {
  const Aabb query = makeAabb(center, size);
  for (const Entity &entity : world.entities) {
    if (!entity.enabled || entity.id == ignoredEntityId ||
        !participatesInCollision(entity)) {
      continue;
    }
    if (queryIntersects(query, colliderAabb(entity))) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> overlapCircle(const World &world, const Vec2 center,
                                       const float radius,
                                       const std::string &layer,
                                       const std::string &ignoredEntityId) {
  std::vector<std::string> hits;
  const float queryRadius = std::max(radius, 0.0F);
  for (const Entity &entity : world.entities) {
    if (!entity.enabled || entity.id == ignoredEntityId ||
        !entity.hasComponent<Transform2DComponent>() || !hasCollider(entity) ||
        (!layer.empty() && colliderLayer(entity) != layer))
      continue;
    if (const auto *circle = entity.component<CircleCollider2DComponent>()) {
      const Transform2DComponent &transform =
          *entity.component<Transform2DComponent>();
      const Vec2 offset = scaledLocalPoint(transform, circle->offset);
      const float cosine = std::cos(transform.rotation);
      const float sine = std::sin(transform.rotation);
      const Vec2 circleCenter{
          transform.position.x + offset.x * cosine - offset.y * sine,
          transform.position.y + offset.x * sine + offset.y * cosine};
      const float dx = center.x - circleCenter.x;
      const float dy = center.y - circleCenter.y;
      const float combinedRadius =
          queryRadius + circle->radius * circleScale(transform);
      if (dx * dx + dy * dy <= combinedRadius * combinedRadius)
        hits.push_back(entity.id);
      continue;
    }
    const Aabb bounds = colliderAabb(entity);
    const float closestX = std::clamp(center.x, bounds.minX, bounds.maxX);
    const float closestY = std::clamp(center.y, bounds.minY, bounds.maxY);
    const float dx = center.x - closestX;
    const float dy = center.y - closestY;
    if (dx * dx + dy * dy <= queryRadius * queryRadius)
      hits.push_back(entity.id);
  }
  std::ranges::sort(hits);
  return hits;
}

std::vector<PhysicsQueryHit2D>
overlapBoxAll(const World &world, const Vec2 center, const Vec2 size,
              const std::string &layer, const std::string &ignoredEntityId) {
  std::vector<PhysicsQueryHit2D> hits;
  const Aabb query = makeAabb(center, size);
  for (const Entity &entity : world.entities) {
    if (!entity.enabled || entity.id == ignoredEntityId ||
        !entity.hasComponent<Transform2DComponent>() || !hasCollider(entity) ||
        (!layer.empty() && colliderLayer(entity) != layer))
      continue;
    const Aabb bounds = colliderAabb(entity);
    if (!queryIntersects(query, bounds))
      continue;
    const Vec2 point{std::clamp(center.x, bounds.minX, bounds.maxX),
                     std::clamp(center.y, bounds.minY, bounds.maxY)};
    const Vec2 delta{center.x - point.x, center.y - point.y};
    const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    hits.push_back({.entityId = entity.id,
                    .layer = colliderLayer(entity),
                    .point = point,
                    .normal = distance > 0.000001F
                                  ? Vec2{delta.x / distance, delta.y / distance}
                                  : Vec2{},
                    .distance = distance});
  }
  std::ranges::sort(hits, {}, &PhysicsQueryHit2D::entityId);
  return hits;
}

std::vector<PhysicsQueryHit2D>
overlapCircleAll(const World &world, const Vec2 center, const float radius,
                 const std::string &layer, const std::string &ignoredEntityId) {
  std::vector<PhysicsQueryHit2D> hits;
  const float queryRadius = std::max(radius, 0.0F);
  for (const Entity &entity : world.entities) {
    if (!entity.enabled || entity.id == ignoredEntityId ||
        !entity.hasComponent<Transform2DComponent>() || !hasCollider(entity) ||
        (!layer.empty() && colliderLayer(entity) != layer))
      continue;
    const Aabb bounds = colliderAabb(entity);
    const Vec2 point{std::clamp(center.x, bounds.minX, bounds.maxX),
                     std::clamp(center.y, bounds.minY, bounds.maxY)};
    const Vec2 delta{center.x - point.x, center.y - point.y};
    const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (distance > queryRadius)
      continue;
    hits.push_back(
        {.entityId = entity.id,
         .layer = colliderLayer(entity),
         .point = point,
         .normal = distance > 0.000001F
                       ? Vec2{delta.x / distance, delta.y / distance}
                       : Vec2{},
         .distance = distance,
         .fraction = queryRadius > 0.0F ? distance / queryRadius : 0.0F});
  }
  std::ranges::sort(hits, {}, &PhysicsQueryHit2D::entityId);
  return hits;
}

template <typename Candidates>
static std::optional<PhysicsRaycastHit2D>
raycastCandidates(const Candidates &candidates, const Vec2 origin,
                  Vec2 direction, const float distance,
                  const std::string &layer,
                  const std::string &ignoredEntityId) {
  const float length =
      std::sqrt(direction.x * direction.x + direction.y * direction.y);
  const float maxDistance = std::max(distance, 0.0F);
  if (length <= 0.000001F || maxDistance <= 0.0F)
    return std::nullopt;
  direction.x /= length;
  direction.y /= length;
  std::optional<PhysicsRaycastHit2D> closest;
  for (const Entity *candidate : candidates) {
    const Entity &entity = *candidate;
    if (!entity.enabled || entity.id == ignoredEntityId ||
        !entity.hasComponent<Transform2DComponent>() || !hasCollider(entity) ||
        (!layer.empty() && colliderLayer(entity) != layer))
      continue;
    if (const auto *circle = entity.component<CircleCollider2DComponent>()) {
      const Transform2DComponent &transform =
          *entity.component<Transform2DComponent>();
      const Vec2 offset = scaledLocalPoint(transform, circle->offset);
      const float cosine = std::cos(transform.rotation);
      const float sine = std::sin(transform.rotation);
      const Vec2 circleCenter{
          transform.position.x + offset.x * cosine - offset.y * sine,
          transform.position.y + offset.x * sine + offset.y * cosine};
      const float radius = circle->radius * circleScale(transform);
      const Vec2 relative{origin.x - circleCenter.x, origin.y - circleCenter.y};
      const float projection =
          relative.x * direction.x + relative.y * direction.y;
      const float discriminant =
          projection * projection -
          (relative.x * relative.x + relative.y * relative.y - radius * radius);
      if (discriminant < 0.0F)
        continue;
      const float hitDistance = -projection - std::sqrt(discriminant);
      if (hitDistance < 0.0F || hitDistance > maxDistance)
        continue;
      if (!closest || hitDistance < closest->distance) {
        const Vec2 point{origin.x + direction.x * hitDistance,
                         origin.y + direction.y * hitDistance};
        closest =
            PhysicsRaycastHit2D{.entityId = entity.id,
                                .layer = colliderLayer(entity),
                                .point = point,
                                .normal = {(point.x - circleCenter.x) / radius,
                                           (point.y - circleCenter.y) / radius},
                                .distance = hitDistance,
                                .fraction = hitDistance / maxDistance};
      }
      continue;
    }
    const auto *box = entity.component<BoxCollider2DComponent>();
    if (box == nullptr) {
      const Aabb genericBounds = colliderAabb(entity);
      float nearTime = 0.0F;
      float farTime = maxDistance;
      Vec2 normal;
      const auto clip = [&](const float start, const float rayDirection,
                            const float minimum, const float maximum,
                            const Vec2 minimumNormal,
                            const Vec2 maximumNormal) {
        if (std::abs(rayDirection) <= 0.000001F)
          return start >= minimum && start <= maximum;
        float first = (minimum - start) / rayDirection;
        float second = (maximum - start) / rayDirection;
        Vec2 firstNormal = minimumNormal;
        if (first > second) {
          std::swap(first, second);
          firstNormal = maximumNormal;
        }
        if (first > nearTime) {
          nearTime = first;
          normal = firstNormal;
        }
        farTime = std::min(farTime, second);
        return nearTime <= farTime;
      };
      if (clip(origin.x, direction.x, genericBounds.minX, genericBounds.maxX,
               {-1.0F, 0.0F}, {1.0F, 0.0F}) &&
          clip(origin.y, direction.y, genericBounds.minY, genericBounds.maxY,
               {0.0F, -1.0F}, {0.0F, 1.0F}) &&
          nearTime >= 0.0F && nearTime <= maxDistance &&
          (!closest || nearTime < closest->distance)) {
        closest =
            PhysicsRaycastHit2D{.entityId = entity.id,
                                .layer = colliderLayer(entity),
                                .point = {origin.x + direction.x * nearTime,
                                          origin.y + direction.y * nearTime},
                                .normal = normal,
                                .distance = nearTime,
                                .fraction = nearTime / maxDistance};
      }
      continue;
    }
    const auto *transform = entity.component<Transform2DComponent>();
    const float cosine = std::cos(transform->rotation);
    const float sine = std::sin(transform->rotation);
    const Vec2 scale = absoluteScale(*transform);
    const Vec2 offset = scaledLocalPoint(*transform, box->offset);
    const Vec2 translatedOrigin{origin.x - transform->position.x,
                                origin.y - transform->position.y};
    const Vec2 localOrigin{
        translatedOrigin.x * cosine + translatedOrigin.y * sine - offset.x,
        -translatedOrigin.x * sine + translatedOrigin.y * cosine - offset.y};
    const Vec2 localDirection{direction.x * cosine + direction.y * sine,
                              -direction.x * sine + direction.y * cosine};
    const Aabb bounds = makeAabb(
        {}, Vec2{.x = box->size.x * scale.x, .y = box->size.y * scale.y});
    float nearTime = 0.0F;
    float farTime = maxDistance;
    Vec2 normal;
    auto clipAxis = [&](const float start, const float rayDirection,
                        const float minimum, const float maximum,
                        const Vec2 minimumNormal, const Vec2 maximumNormal) {
      if (std::abs(rayDirection) <= 0.000001F)
        return start >= minimum && start <= maximum;
      float first = (minimum - start) / rayDirection;
      float second = (maximum - start) / rayDirection;
      Vec2 firstNormal = minimumNormal;
      if (first > second) {
        std::swap(first, second);
        firstNormal = maximumNormal;
      }
      if (first > nearTime) {
        nearTime = first;
        normal = firstNormal;
      }
      farTime = std::min(farTime, second);
      return nearTime <= farTime;
    };
    if (!clipAxis(localOrigin.x, localDirection.x, bounds.minX, bounds.maxX,
                  {-1.0F, 0.0F}, {1.0F, 0.0F}) ||
        !clipAxis(localOrigin.y, localDirection.y, bounds.minY, bounds.maxY,
                  {0.0F, -1.0F}, {0.0F, 1.0F}) ||
        nearTime < 0.0F || nearTime > maxDistance)
      continue;
    if (!closest || nearTime < closest->distance) {
      closest =
          PhysicsRaycastHit2D{.entityId = entity.id,
                              .layer = colliderLayer(entity),
                              .point = {origin.x + direction.x * nearTime,
                                        origin.y + direction.y * nearTime},
                              .normal = {normal.x * cosine - normal.y * sine,
                                         normal.x * sine + normal.y * cosine},
                              .distance = nearTime,
                              .fraction = nearTime / maxDistance};
    }
  }
  return closest;
}

std::optional<PhysicsRaycastHit2D>
raycast2D(const World &world, Vec2 origin, Vec2 direction, float distance,
          const std::string &layer, const std::string &ignoredEntityId) {
  const auto candidates =
      world.entities |
      std::views::transform([](const Entity &entity) { return &entity; });
  return raycastCandidates(candidates, origin, direction, distance, layer,
                           ignoredEntityId);
}

RadialRaycast2D::RadialRaycast2D(const World &world, Vec2 origin, float radius,
                                 const std::string &layer, const std::string &ignoredEntityId)
    : origin_(origin), radius_(radius), layer_(layer), ignoredEntityId_(ignoredEntityId) {
  // Filter once for an entire fan of rays. The exact shared shape tests still
  // decide each hit; the bounds only reject colliders no ray can reach.
  for (const Entity &entity : world.entities) {
    if (!entity.enabled || !entity.hasComponent<Transform2DComponent>() ||
        !hasCollider(entity) ||
        (!layer.empty() && colliderLayer(entity) != layer)) {
      continue;
    }
    const Aabb bounds = colliderAabb(entity);
    const float dx = origin.x - std::clamp(origin.x, bounds.minX, bounds.maxX);
    const float dy = origin.y - std::clamp(origin.y, bounds.minY, bounds.maxY);
    if (dx * dx + dy * dy <= radius * radius) {
      candidates_.push_back(&entity);
    }
  }
}

std::vector<float> RadialRaycast2D::silhouetteAngles() const {
  constexpr float tau = 2 * std::numbers::pi_v<float>;
  std::vector<float> angles;
  const auto append = [&](Vec2 point) {
    const Vec2 relative{point.x - origin_.x, point.y - origin_.y};
    if (relative.x == 0 && relative.y == 0) {
      return;
    }
    float angle = std::atan2(relative.y, relative.x);
    if (angle < 0) {
      angle += tau;
    }
    if (angle >= tau) {
      angle = 0;
    }
    angles.push_back(angle);
  };
  for (const Entity *entity : candidates_) {
    if (entity->id == ignoredEntityId_) {
      continue;
    }
    const auto &transform = *entity->component<Transform2DComponent>();
    const float cosine = std::cos(transform.rotation);
    const float sine = std::sin(transform.rotation);
    const auto worldPoint = [&](Vec2 local) {
      const Vec2 scaled = scaledLocalPoint(transform, local);
      return Vec2{transform.position.x + scaled.x * cosine - scaled.y * sine,
                   transform.position.y + scaled.x * sine + scaled.y * cosine};
    };
    if (const auto *circle = entity->component<CircleCollider2DComponent>()) {
      const Vec2 center = worldPoint(circle->offset);
      const Vec2 relative{center.x - origin_.x, center.y - origin_.y};
      const float distance = std::hypot(relative.x, relative.y);
      const float radius = circle->radius * circleScale(transform);
      if (distance > radius && radius > 0) {
        const float centerAngle = std::atan2(relative.y, relative.x);
        const float tangentOffset = std::asin(radius / distance);
        for (float angle : {centerAngle - tangentOffset,
                            centerAngle + tangentOffset}) {
          angle = std::fmod(angle + tau, tau);
          angles.push_back(angle);
        }
      }
    } else if (const auto *box = entity->component<BoxCollider2DComponent>()) {
      const Vec2 half{box->size.x * .5F, box->size.y * .5F};
      for (const Vec2 corner : {Vec2{-half.x, -half.y}, Vec2{half.x, -half.y},
                                Vec2{half.x, half.y}, Vec2{-half.x, half.y}}) {
        append(worldPoint({box->offset.x + corner.x,
                            box->offset.y + corner.y}));
      }
    } else {
      // Match the shared native ray query, which currently intersects other
      // collider shapes through their world AABB rather than polygon edges.
      const Aabb bounds = colliderAabb(*entity);
      append({bounds.minX, bounds.minY});
      append({bounds.maxX, bounds.minY});
      append({bounds.maxX, bounds.maxY});
      append({bounds.minX, bounds.maxY});
    }
  }
  std::ranges::sort(angles);
  angles.erase(std::unique(angles.begin(), angles.end()), angles.end());
  return angles;
}

std::optional<PhysicsRaycastHit2D> RadialRaycast2D::cast(Vec2 direction) const {
  return cast(direction, radius_);
}

std::optional<PhysicsRaycastHit2D> RadialRaycast2D::cast(Vec2 direction, float distance) const {
  return raycastCandidates(candidates_, origin_, direction, std::min(distance, radius_), layer_, ignoredEntityId_);
}

PhysicsRaycastBatch2DResult raycastBatch2D(const World &world, Vec2 origin,
    std::span<const PhysicsRay2D> rays, const std::string &layer, const std::string &ignoredEntityId) {
  ProfileScope batchScope("Physics2D.raycast_batch");
  if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || rays.size() > 4096) {
    return {{}, "Ray batch requires a finite origin and at most 4096 rays."};
  }
  float radius = 0;
  for (const PhysicsRay2D &ray : rays) {
    if (!std::isfinite(ray.direction.x) || !std::isfinite(ray.direction.y) ||
        !std::isfinite(ray.distance) || ray.distance < 0) {
      return {{}, "Ray directions must be finite and distances finite and non-negative."};
    }
    radius = std::max(radius, ray.distance);
  }
  PhysicsRaycastBatch2DResult result;
  result.hits.reserve(rays.size());
  if (rays.empty()) { return result; }
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
