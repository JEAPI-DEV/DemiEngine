#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include "demi/runtime/scene/WorldQueries.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#if DEMI_HAS_BOX2D
#include <box2d/box2d.h>
#endif

namespace demi::runtime {

namespace {

constexpr float QueryContactSlop = 0.06F;

struct Aabb {
  float minX = 0.0F;
  float minY = 0.0F;
  float maxX = 0.0F;
  float maxY = 0.0F;
};

[[nodiscard]] bool participatesInCollision(const Entity &entity) {
  if (!entity.hasComponent<Transform2DComponent>())
    return false;
  if (const auto *box = entity.component<BoxCollider2DComponent>())
    return !box->isTrigger;
  if (const auto *circle = entity.component<CircleCollider2DComponent>())
    return !circle->isTrigger;
  return false;
}

[[nodiscard]] Aabb colliderAabb(const Entity &entity) {
  const Vec2 position = entity.component<Transform2DComponent>()->position;
  Vec2 offset;
  Vec2 size;
  if (const auto *box = entity.component<BoxCollider2DComponent>()) {
    offset = box->offset;
    size = box->size;
  } else if (const auto *circle =
                 entity.component<CircleCollider2DComponent>()) {
    offset = circle->offset;
    size = {circle->radius * 2.0F, circle->radius * 2.0F};
  }
  const Vec2 center{.x = position.x + offset.x, .y = position.y + offset.y};
  return Aabb{
      .minX = center.x - size.x * 0.5F,
      .minY = center.y - size.y * 0.5F,
      .maxX = center.x + size.x * 0.5F,
      .maxY = center.y + size.y * 0.5F,
  };
}

[[nodiscard]] std::string colliderLayer(const Entity &entity) {
  if (const auto *box = entity.component<BoxCollider2DComponent>())
    return box->layer;
  if (const auto *circle = entity.component<CircleCollider2DComponent>())
    return circle->layer;
  return {};
}

[[nodiscard]] bool hasCollider(const Entity &entity) {
  return entity.hasComponent<BoxCollider2DComponent>() ||
         entity.hasComponent<CircleCollider2DComponent>();
}

[[nodiscard]] bool queryIntersects(const Aabb &a, const Aabb &b) {
  return a.minX <= b.maxX + QueryContactSlop &&
         a.maxX >= b.minX - QueryContactSlop &&
         a.minY <= b.maxY + QueryContactSlop &&
         a.maxY >= b.minY - QueryContactSlop;
}

[[nodiscard]] Aabb makeAabb(const Vec2 center, const Vec2 size) {
  return Aabb{
      .minX = center.x - size.x * 0.5F,
      .minY = center.y - size.y * 0.5F,
      .maxX = center.x + size.x * 0.5F,
      .maxY = center.y + size.y * 0.5F,
  };
}

#if !DEMI_HAS_BOX2D

[[nodiscard]] bool intersects(const Aabb &a, const Aabb &b) {
  return a.minX < b.maxX && a.maxX > b.minX && a.minY < b.maxY &&
         a.maxY > b.minY;
}

[[nodiscard]] bool isDynamic(const Entity &entity) {
  return entity.hasComponent<Rigidbody2DComponent>() &&
         entity.component<Rigidbody2DComponent>()->bodyType == "dynamic";
}

void resolveAxis(World &world, Entity &moving, const Vec2 delta,
                 const bool horizontal) {
  if (!moving.hasComponent<Transform2DComponent>() ||
      !moving.hasComponent<Rigidbody2DComponent>()) {
    return;
  }

  const std::optional<Aabb> previousAabb =
      moving.hasComponent<BoxCollider2DComponent>()
          ? std::optional<Aabb>{colliderAabb(moving)}
          : std::nullopt;

  moving.component<Transform2DComponent>()->position.x += delta.x;
  moving.component<Transform2DComponent>()->position.y += delta.y;

  if (!moving.hasComponent<BoxCollider2DComponent>()) {
    return;
  }

  for (const Entity &other : world.entities) {
    if (other.id == moving.id || !participatesInCollision(other)) {
      continue;
    }

    const Aabb movingAabb = colliderAabb(moving);
    const Aabb otherAabb = colliderAabb(other);
    if (!intersects(movingAabb, otherAabb)) {
      continue;
    }

    if (horizontal) {
      if (previousAabb.has_value() && previousAabb->maxX <= otherAabb.minX) {
        moving.component<Transform2DComponent>()->position.x -=
            movingAabb.maxX - otherAabb.minX;
      } else if (previousAabb.has_value() &&
                 previousAabb->minX >= otherAabb.maxX) {
        moving.component<Transform2DComponent>()->position.x +=
            otherAabb.maxX - movingAabb.minX;
      } else {
        continue;
      }
      moving.component<Rigidbody2DComponent>()->velocity.x = 0.0F;
    } else {
      const float incomingVelocity =
          moving.component<Rigidbody2DComponent>()->velocity.y;
      if (previousAabb.has_value() && previousAabb->minY >= otherAabb.maxY) {
        moving.component<Transform2DComponent>()->position.y +=
            otherAabb.maxY - movingAabb.minY;
      } else if (previousAabb.has_value() &&
                 previousAabb->maxY <= otherAabb.minY) {
        moving.component<Transform2DComponent>()->position.y -=
            movingAabb.maxY - otherAabb.minY;
      } else if (delta.y < 0.0F) {
        moving.component<Transform2DComponent>()->position.y +=
            otherAabb.maxY - movingAabb.minY;
      } else if (delta.y > 0.0F) {
        moving.component<Transform2DComponent>()->position.y -=
            movingAabb.maxY - otherAabb.minY;
      } else {
        continue;
      }

      const float bounciness = std::clamp(
          moving.component<Rigidbody2DComponent>()->bounciness, 0.0F, 1.0F);
      if (bounciness > 0.0F && std::abs(incomingVelocity) > 2.0F) {
        moving.component<Rigidbody2DComponent>()->velocity.y =
            -incomingVelocity * bounciness;
      } else {
        moving.component<Rigidbody2DComponent>()->velocity.y = 0.0F;
      }
    }
  }
}

#endif

} // namespace

std::optional<Vec2> rigidbodyVelocity(const World &world,
                                      const std::string &entityId) {
  const Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return std::nullopt;
  }
  return entity->component<Rigidbody2DComponent>()->velocity;
}

bool setRigidbodyVelocity(World &world, const std::string &entityId,
                          const Vec2 velocity) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return false;
  }
  entity->component<Rigidbody2DComponent>()->velocity = velocity;
  return true;
}

bool setRigidbodyVelocityX(World &world, const std::string &entityId,
                           const float x) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return false;
  }
  entity->component<Rigidbody2DComponent>()->velocity.x = x;
  return true;
}

bool setRigidbodyVelocityY(World &world, const std::string &entityId,
                           const float y) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return false;
  }
  entity->component<Rigidbody2DComponent>()->velocity.y = y;
  return true;
}

bool addRigidbodyImpulse(World &world, const std::string &entityId,
                         const Vec2 impulse) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return false;
  }
  entity->component<Rigidbody2DComponent>()->velocity.x += impulse.x;
  entity->component<Rigidbody2DComponent>()->velocity.y += impulse.y;
  return true;
}

bool overlapBox(const World &world, const Vec2 center, const Vec2 size,
                const std::string &ignoredEntityId) {
  const Aabb query = makeAabb(center, size);
  for (const Entity &entity : world.entities) {
    if (entity.id == ignoredEntityId || !participatesInCollision(entity)) {
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
    if (entity.id == ignoredEntityId ||
        !entity.hasComponent<Transform2DComponent>() || !hasCollider(entity) ||
        (!layer.empty() && colliderLayer(entity) != layer))
      continue;
    if (const auto *circle = entity.component<CircleCollider2DComponent>()) {
      const Vec2 position = entity.component<Transform2DComponent>()->position;
      const float dx = center.x - (position.x + circle->offset.x);
      const float dy = center.y - (position.y + circle->offset.y);
      const float combinedRadius = queryRadius + circle->radius;
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

std::optional<PhysicsRaycastHit2D>
raycast2D(const World &world, const Vec2 origin, Vec2 direction,
          const float distance, const std::string &layer,
          const std::string &ignoredEntityId) {
  const float length =
      std::sqrt(direction.x * direction.x + direction.y * direction.y);
  const float maxDistance = std::max(distance, 0.0F);
  if (length <= 0.000001F || maxDistance <= 0.0F)
    return std::nullopt;
  direction.x /= length;
  direction.y /= length;
  std::optional<PhysicsRaycastHit2D> closest;
  for (const Entity &entity : world.entities) {
    if (entity.id == ignoredEntityId ||
        !entity.hasComponent<Transform2DComponent>() || !hasCollider(entity) ||
        (!layer.empty() && colliderLayer(entity) != layer))
      continue;
    if (const auto *circle = entity.component<CircleCollider2DComponent>()) {
      const Vec2 entityPosition =
          entity.component<Transform2DComponent>()->position;
      const Vec2 circleCenter{entityPosition.x + circle->offset.x,
                              entityPosition.y + circle->offset.y};
      const Vec2 relative{origin.x - circleCenter.x, origin.y - circleCenter.y};
      const float projection =
          relative.x * direction.x + relative.y * direction.y;
      const float discriminant =
          projection * projection -
          (relative.x * relative.x + relative.y * relative.y -
           circle->radius * circle->radius);
      if (discriminant < 0.0F)
        continue;
      const float hitDistance = -projection - std::sqrt(discriminant);
      if (hitDistance < 0.0F || hitDistance > maxDistance)
        continue;
      if (!closest || hitDistance < closest->distance) {
        const Vec2 point{origin.x + direction.x * hitDistance,
                         origin.y + direction.y * hitDistance};
        closest = PhysicsRaycastHit2D{
            .entityId = entity.id,
            .point = point,
            .normal = {(point.x - circleCenter.x) / circle->radius,
                       (point.y - circleCenter.y) / circle->radius},
            .distance = hitDistance};
      }
      continue;
    }
    const Aabb bounds = colliderAabb(entity);
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
    if (!clipAxis(origin.x, direction.x, bounds.minX, bounds.maxX,
                  {-1.0F, 0.0F}, {1.0F, 0.0F}) ||
        !clipAxis(origin.y, direction.y, bounds.minY, bounds.maxY,
                  {0.0F, -1.0F}, {0.0F, 1.0F}) ||
        nearTime < 0.0F || nearTime > maxDistance)
      continue;
    if (!closest || nearTime < closest->distance) {
      closest =
          PhysicsRaycastHit2D{.entityId = entity.id,
                              .point = {origin.x + direction.x * nearTime,
                                        origin.y + direction.y * nearTime},
                              .normal = normal,
                              .distance = nearTime};
    }
  }
  return closest;
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

void stepPhysics2D(World &world, const float fixedDt,
                   const PhysicsSettings2D &settings) {
  world.physicsContacts.clear();
#if DEMI_HAS_BOX2D
  b2World physicsWorld({settings.gravity.x, settings.gravity.y});

  struct BodyBinding {
    Entity *entity = nullptr;
    b2Body *body = nullptr;
  };
  std::vector<BodyBinding> bindings;

  for (Entity &entity : world.entities) {
    if (!entity.hasComponent<Transform2DComponent>()) {
      continue;
    }
    if (!entity.hasComponent<Rigidbody2DComponent>() && !hasCollider(entity)) {
      continue;
    }

    b2BodyDef bodyDef;
    bodyDef.position.Set(entity.component<Transform2DComponent>()->position.x,
                         entity.component<Transform2DComponent>()->position.y);
    bodyDef.angle = entity.component<Transform2DComponent>()->rotation;
    if (entity.hasComponent<Rigidbody2DComponent>()) {
      if (entity.component<Rigidbody2DComponent>()->bodyType == "dynamic") {
        bodyDef.type = b2_dynamicBody;
      } else if (entity.component<Rigidbody2DComponent>()->bodyType ==
                 "kinematic") {
        bodyDef.type = b2_kinematicBody;
      } else {
        bodyDef.type = b2_staticBody;
      }
      bodyDef.linearVelocity.Set(
          entity.component<Rigidbody2DComponent>()->velocity.x,
          entity.component<Rigidbody2DComponent>()->velocity.y);
      bodyDef.fixedRotation =
          entity.component<Rigidbody2DComponent>()->lockRotation;
      bodyDef.gravityScale =
          entity.component<Rigidbody2DComponent>()->gravityScale;
    } else {
      bodyDef.type = b2_staticBody;
    }

    b2Body *body = physicsWorld.CreateBody(&bodyDef);
    body->GetUserData().pointer = reinterpret_cast<std::uintptr_t>(&entity);
    if (entity.hasComponent<BoxCollider2DComponent>()) {
      b2PolygonShape shape;
      shape.SetAsBox(entity.component<BoxCollider2DComponent>()->size.x * 0.5F,
                     entity.component<BoxCollider2DComponent>()->size.y * 0.5F,
                     {entity.component<BoxCollider2DComponent>()->offset.x,
                      entity.component<BoxCollider2DComponent>()->offset.y},
                     0.0F);
      b2FixtureDef fixtureDef;
      fixtureDef.shape = &shape;
      fixtureDef.density = 1.0F;
      fixtureDef.restitution =
          entity.hasComponent<Rigidbody2DComponent>()
              ? std::clamp(entity.component<Rigidbody2DComponent>()->bounciness,
                           0.0F, 1.0F)
              : 0.0F;
      fixtureDef.isSensor =
          entity.component<BoxCollider2DComponent>()->isTrigger;
      fixtureDef.filter.categoryBits =
          entity.component<BoxCollider2DComponent>()->categoryBits;
      fixtureDef.filter.maskBits =
          entity.component<BoxCollider2DComponent>()->maskBits;
      if (const auto category = world.physicsCategoryBits.find(
              entity.component<BoxCollider2DComponent>()->layer);
          category != world.physicsCategoryBits.end()) {
        fixtureDef.filter.categoryBits = category->second;
        fixtureDef.filter.maskBits = world.physicsMaskBits.at(
            entity.component<BoxCollider2DComponent>()->layer);
      }
      body->CreateFixture(&fixtureDef);
    }
    if (entity.hasComponent<CircleCollider2DComponent>()) {
      const CircleCollider2DComponent &collider =
          *entity.component<CircleCollider2DComponent>();
      b2CircleShape shape;
      shape.m_p.Set(collider.offset.x, collider.offset.y);
      shape.m_radius = collider.radius;
      b2FixtureDef fixtureDef;
      fixtureDef.shape = &shape;
      fixtureDef.density = 1.0F;
      fixtureDef.restitution =
          entity.hasComponent<Rigidbody2DComponent>()
              ? std::clamp(entity.component<Rigidbody2DComponent>()->bounciness,
                           0.0F, 1.0F)
              : 0.0F;
      fixtureDef.isSensor = collider.isTrigger;
      fixtureDef.filter.categoryBits = collider.categoryBits;
      fixtureDef.filter.maskBits = collider.maskBits;
      if (const auto category = world.physicsCategoryBits.find(collider.layer);
          category != world.physicsCategoryBits.end()) {
        fixtureDef.filter.categoryBits = category->second;
        fixtureDef.filter.maskBits = world.physicsMaskBits.at(collider.layer);
      }
      body->CreateFixture(&fixtureDef);
    }
    bindings.push_back(BodyBinding{.entity = &entity, .body = body});
  }

  const auto bodyForEntity = [&](const std::string &id) -> b2Body * {
    const auto found = std::ranges::find_if(bindings, [&](const auto &binding) {
      return binding.entity != nullptr && binding.entity->id == id;
    });
    return found == bindings.end() ? nullptr : found->body;
  };
  for (const BodyBinding &binding : bindings) {
    if (binding.entity == nullptr || binding.body == nullptr)
      continue;
    const auto *joint = binding.entity->component<DistanceJoint2DComponent>();
    b2Body *other =
        joint == nullptr ? nullptr : bodyForEntity(joint->otherEntity);
    if (joint == nullptr || other == nullptr || other == binding.body)
      continue;
    const b2Vec2 positionA = binding.body->GetPosition();
    const b2Vec2 positionB = other->GetPosition();
    b2DistanceJointDef definition;
    definition.Initialize(
        binding.body, other,
        {positionA.x + joint->anchor.x, positionA.y + joint->anchor.y},
        {positionB.x + joint->otherAnchor.x,
         positionB.y + joint->otherAnchor.y});
    definition.length = joint->length;
    definition.minLength = joint->length;
    definition.maxLength = joint->length;
    definition.stiffness = joint->stiffness;
    definition.damping = joint->damping;
    definition.collideConnected = joint->collideConnected;
    physicsWorld.CreateJoint(&definition);
  }

  physicsWorld.Step(fixedDt, 8, 3);

  for (b2Contact *contact = physicsWorld.GetContactList(); contact != nullptr;
       contact = contact->GetNext()) {
    if (!contact->IsTouching()) {
      continue;
    }

    b2Fixture *fixtureA = contact->GetFixtureA();
    b2Fixture *fixtureB = contact->GetFixtureB();
    Entity *entityA =
        reinterpret_cast<Entity *>(fixtureA->GetBody()->GetUserData().pointer);
    Entity *entityB =
        reinterpret_cast<Entity *>(fixtureB->GetBody()->GetUserData().pointer);
    if (entityA == nullptr || entityB == nullptr) {
      continue;
    }

    b2WorldManifold manifold;
    contact->GetWorldManifold(&manifold);
    const bool trigger = fixtureA->IsSensor() || fixtureB->IsSensor();
    const std::string layerA = colliderLayer(*entityA);
    const std::string layerB = colliderLayer(*entityB);
    world.physicsContacts.push_back(PhysicsContact2D{
        .entityId = entityA->id,
        .otherEntityId = entityB->id,
        .otherLayer = layerB,
        .normal = Vec2{.x = -manifold.normal.x, .y = -manifold.normal.y},
        .isTrigger = trigger,
    });
    world.physicsContacts.push_back(PhysicsContact2D{
        .entityId = entityB->id,
        .otherEntityId = entityA->id,
        .otherLayer = layerA,
        .normal = Vec2{.x = manifold.normal.x, .y = manifold.normal.y},
        .isTrigger = trigger,
    });
  }

  for (const BodyBinding &binding : bindings) {
    if (binding.entity == nullptr || binding.body == nullptr ||
        !binding.entity->hasComponent<Transform2DComponent>()) {
      continue;
    }
    const b2Vec2 position = binding.body->GetPosition();
    binding.entity->component<Transform2DComponent>()->position =
        Vec2{.x = position.x, .y = position.y};
    binding.entity->component<Transform2DComponent>()->rotation =
        binding.body->GetAngle();
    if (binding.entity->hasComponent<Rigidbody2DComponent>()) {
      const b2Vec2 velocity = binding.body->GetLinearVelocity();
      binding.entity->component<Rigidbody2DComponent>()->velocity =
          Vec2{.x = velocity.x, .y = velocity.y};
    }
  }
#else
  for (Entity &entity : world.entities) {
    if (!isDynamic(entity) || !entity.hasComponent<Transform2DComponent>()) {
      continue;
    }

    entity.component<Rigidbody2DComponent>()->velocity.x = std::clamp(
        entity.component<Rigidbody2DComponent>()->velocity.x, -100.0F, 100.0F);
    entity.component<Rigidbody2DComponent>()->velocity.y +=
        settings.gravity.y *
        entity.component<Rigidbody2DComponent>()->gravityScale * fixedDt;
    entity.component<Rigidbody2DComponent>()->velocity.y = std::clamp(
        entity.component<Rigidbody2DComponent>()->velocity.y, -100.0F, 100.0F);

    resolveAxis(world, entity,
                Vec2{.x = entity.component<Rigidbody2DComponent>()->velocity.x *
                          fixedDt,
                     .y = 0.0F},
                true);
    resolveAxis(world, entity,
                Vec2{.x = 0.0F,
                     .y = entity.component<Rigidbody2DComponent>()->velocity.y *
                          fixedDt},
                false);
  }
#endif
}

} // namespace demi::runtime
