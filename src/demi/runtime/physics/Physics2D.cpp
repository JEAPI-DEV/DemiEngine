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
  return entity.hasComponent<Transform2DComponent>() &&
         entity.hasComponent<BoxCollider2DComponent>() &&
         !entity.component<BoxCollider2DComponent>()->isTrigger;
}

[[nodiscard]] Aabb colliderAabb(const Entity &entity) {
  const Vec2 position = entity.component<Transform2DComponent>()->position;
  const Vec2 offset = entity.component<BoxCollider2DComponent>()->offset;
  const Vec2 size = entity.component<BoxCollider2DComponent>()->size;
  const Vec2 center{.x = position.x + offset.x, .y = position.y + offset.y};
  return Aabb{
      .minX = center.x - size.x * 0.5F,
      .minY = center.y - size.y * 0.5F,
      .maxX = center.x + size.x * 0.5F,
      .maxY = center.y + size.y * 0.5F,
  };
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
    if (!entity.hasComponent<Rigidbody2DComponent>() &&
        !entity.hasComponent<BoxCollider2DComponent>()) {
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
      body->CreateFixture(&fixtureDef);
    }
    bindings.push_back(BodyBinding{.entity = &entity, .body = body});
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
    const std::string layerA =
        entityA->hasComponent<BoxCollider2DComponent>()
            ? entityA->component<BoxCollider2DComponent>()->layer
            : std::string{};
    const std::string layerB =
        entityB->hasComponent<BoxCollider2DComponent>()
            ? entityB->component<BoxCollider2DComponent>()->layer
            : std::string{};
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
