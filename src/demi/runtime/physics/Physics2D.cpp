#include "demi/runtime/physics/Physics2D.h"

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
  return entity.transform2D.has_value() && entity.boxCollider2D.has_value() &&
         !entity.boxCollider2D->isTrigger;
}

[[nodiscard]] Aabb colliderAabb(const Entity &entity) {
  const Vec2 position = entity.transform2D->position;
  const Vec2 offset = entity.boxCollider2D->offset;
  const Vec2 size = entity.boxCollider2D->size;
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
  return entity.rigidbody2D.has_value() &&
         entity.rigidbody2D->bodyType == "dynamic";
}

void resolveAxis(World &world, Entity &moving, const Vec2 delta,
                 const bool horizontal) {
  if (!moving.transform2D.has_value() || !moving.rigidbody2D.has_value()) {
    return;
  }

  const std::optional<Aabb> previousAabb =
      moving.boxCollider2D.has_value()
          ? std::optional<Aabb>{colliderAabb(moving)}
          : std::nullopt;

  moving.transform2D->position.x += delta.x;
  moving.transform2D->position.y += delta.y;

  if (!moving.boxCollider2D.has_value()) {
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
        moving.transform2D->position.x -= movingAabb.maxX - otherAabb.minX;
      } else if (previousAabb.has_value() &&
                 previousAabb->minX >= otherAabb.maxX) {
        moving.transform2D->position.x += otherAabb.maxX - movingAabb.minX;
      } else {
        continue;
      }
      moving.rigidbody2D->velocity.x = 0.0F;
    } else {
      const float incomingVelocity = moving.rigidbody2D->velocity.y;
      if (previousAabb.has_value() && previousAabb->minY >= otherAabb.maxY) {
        moving.transform2D->position.y += otherAabb.maxY - movingAabb.minY;
      } else if (previousAabb.has_value() &&
                 previousAabb->maxY <= otherAabb.minY) {
        moving.transform2D->position.y -= movingAabb.maxY - otherAabb.minY;
      } else if (delta.y < 0.0F) {
        moving.transform2D->position.y += otherAabb.maxY - movingAabb.minY;
      } else if (delta.y > 0.0F) {
        moving.transform2D->position.y -= movingAabb.maxY - otherAabb.minY;
      } else {
        continue;
      }

      const float bounciness =
          std::clamp(moving.rigidbody2D->bounciness, 0.0F, 1.0F);
      if (bounciness > 0.0F && std::abs(incomingVelocity) > 2.0F) {
        moving.rigidbody2D->velocity.y = -incomingVelocity * bounciness;
      } else {
        moving.rigidbody2D->velocity.y = 0.0F;
      }
    }
  }
}

#endif

} // namespace

std::optional<Vec2> rigidbodyVelocity(const World &world,
                                      const std::string &entityId) {
  const Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->rigidbody2D.has_value()) {
    return std::nullopt;
  }
  return entity->rigidbody2D->velocity;
}

bool setRigidbodyVelocity(World &world, const std::string &entityId,
                          const Vec2 velocity) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->rigidbody2D.has_value()) {
    return false;
  }
  entity->rigidbody2D->velocity = velocity;
  return true;
}

bool setRigidbodyVelocityX(World &world, const std::string &entityId,
                           const float x) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->rigidbody2D.has_value()) {
    return false;
  }
  entity->rigidbody2D->velocity.x = x;
  return true;
}

bool setRigidbodyVelocityY(World &world, const std::string &entityId,
                           const float y) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->rigidbody2D.has_value()) {
    return false;
  }
  entity->rigidbody2D->velocity.y = y;
  return true;
}

bool addRigidbodyImpulse(World &world, const std::string &entityId,
                         const Vec2 impulse) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->rigidbody2D.has_value()) {
    return false;
  }
  entity->rigidbody2D->velocity.x += impulse.x;
  entity->rigidbody2D->velocity.y += impulse.y;
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
    if (!entity.transform2D.has_value()) {
      continue;
    }
    if (!entity.rigidbody2D.has_value() && !entity.boxCollider2D.has_value()) {
      continue;
    }

    b2BodyDef bodyDef;
    bodyDef.position.Set(entity.transform2D->position.x,
                         entity.transform2D->position.y);
    bodyDef.angle = entity.transform2D->rotation;
    if (entity.rigidbody2D.has_value()) {
      if (entity.rigidbody2D->bodyType == "dynamic") {
        bodyDef.type = b2_dynamicBody;
      } else if (entity.rigidbody2D->bodyType == "kinematic") {
        bodyDef.type = b2_kinematicBody;
      } else {
        bodyDef.type = b2_staticBody;
      }
      bodyDef.linearVelocity.Set(entity.rigidbody2D->velocity.x,
                                 entity.rigidbody2D->velocity.y);
      bodyDef.fixedRotation = entity.rigidbody2D->lockRotation;
      bodyDef.gravityScale = entity.rigidbody2D->gravityScale;
    } else {
      bodyDef.type = b2_staticBody;
    }

    b2Body *body = physicsWorld.CreateBody(&bodyDef);
    body->GetUserData().pointer = reinterpret_cast<std::uintptr_t>(&entity);
    if (entity.boxCollider2D.has_value()) {
      b2PolygonShape shape;
      shape.SetAsBox(
          entity.boxCollider2D->size.x * 0.5F,
          entity.boxCollider2D->size.y * 0.5F,
          {entity.boxCollider2D->offset.x, entity.boxCollider2D->offset.y},
          0.0F);
      b2FixtureDef fixtureDef;
      fixtureDef.shape = &shape;
      fixtureDef.density = 1.0F;
      fixtureDef.restitution =
          entity.rigidbody2D.has_value()
              ? std::clamp(entity.rigidbody2D->bounciness, 0.0F, 1.0F)
              : 0.0F;
      fixtureDef.isSensor = entity.boxCollider2D->isTrigger;
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
    const std::string layerA = entityA->boxCollider2D.has_value()
                                   ? entityA->boxCollider2D->layer
                                   : std::string{};
    const std::string layerB = entityB->boxCollider2D.has_value()
                                   ? entityB->boxCollider2D->layer
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
        !binding.entity->transform2D.has_value()) {
      continue;
    }
    const b2Vec2 position = binding.body->GetPosition();
    binding.entity->transform2D->position =
        Vec2{.x = position.x, .y = position.y};
    binding.entity->transform2D->rotation = binding.body->GetAngle();
    if (binding.entity->rigidbody2D.has_value()) {
      const b2Vec2 velocity = binding.body->GetLinearVelocity();
      binding.entity->rigidbody2D->velocity =
          Vec2{.x = velocity.x, .y = velocity.y};
    }
  }
#else
  for (Entity &entity : world.entities) {
    if (!isDynamic(entity) || !entity.transform2D.has_value()) {
      continue;
    }

    entity.rigidbody2D->velocity.x =
        std::clamp(entity.rigidbody2D->velocity.x, -100.0F, 100.0F);
    entity.rigidbody2D->velocity.y +=
        settings.gravity.y * entity.rigidbody2D->gravityScale * fixedDt;
    entity.rigidbody2D->velocity.y =
        std::clamp(entity.rigidbody2D->velocity.y, -100.0F, 100.0F);

    resolveAxis(world, entity,
                Vec2{.x = entity.rigidbody2D->velocity.x * fixedDt, .y = 0.0F},
                true);
    resolveAxis(world, entity,
                Vec2{.x = 0.0F, .y = entity.rigidbody2D->velocity.y * fixedDt},
                false);
  }
#endif
}

} // namespace demi::runtime
