#include "demi/runtime/Physics2D.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime {

namespace {

struct Aabb {
  float minX = 0.0F;
  float minY = 0.0F;
  float maxX = 0.0F;
  float maxY = 0.0F;
};

[[nodiscard]] bool isDynamic(const Entity& entity) {
  return entity.rigidbody2D.has_value() && entity.rigidbody2D->bodyType == "dynamic";
}

[[nodiscard]] bool participatesInCollision(const Entity& entity) {
  return entity.transform2D.has_value() && entity.boxCollider2D.has_value() && !entity.boxCollider2D->isTrigger;
}

[[nodiscard]] Aabb colliderAabb(const Entity& entity) {
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

[[nodiscard]] bool intersects(const Aabb& a, const Aabb& b) {
  return a.minX < b.maxX && a.maxX > b.minX && a.minY < b.maxY && a.maxY > b.minY;
}

[[nodiscard]] Aabb makeAabb(const Vec2 center, const Vec2 size) {
  return Aabb{
    .minX = center.x - size.x * 0.5F,
    .minY = center.y - size.y * 0.5F,
    .maxX = center.x + size.x * 0.5F,
    .maxY = center.y + size.y * 0.5F,
  };
}

void resolveAxis(World& world, Entity& moving, const Vec2 delta, const bool horizontal) {
  if (!moving.transform2D.has_value() || !moving.rigidbody2D.has_value()) {
    return;
  }

  const std::optional<Aabb> previousAabb = moving.boxCollider2D.has_value() ? std::optional<Aabb>{colliderAabb(moving)} : std::nullopt;

  moving.transform2D->position.x += delta.x;
  moving.transform2D->position.y += delta.y;

  if (!moving.boxCollider2D.has_value()) {
    return;
  }

  for (const Entity& other : world.entities) {
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
      } else if (previousAabb.has_value() && previousAabb->minX >= otherAabb.maxX) {
        moving.transform2D->position.x += otherAabb.maxX - movingAabb.minX;
      } else {
        continue;
      }
      moving.rigidbody2D->velocity.x = 0.0F;
    } else {
      const float incomingVelocity = moving.rigidbody2D->velocity.y;
      if (previousAabb.has_value() && previousAabb->minY >= otherAabb.maxY) {
        moving.transform2D->position.y += otherAabb.maxY - movingAabb.minY;
      } else if (previousAabb.has_value() && previousAabb->maxY <= otherAabb.minY) {
        moving.transform2D->position.y -= movingAabb.maxY - otherAabb.minY;
      } else if (delta.y < 0.0F) {
        moving.transform2D->position.y += otherAabb.maxY - movingAabb.minY;
      } else if (delta.y > 0.0F) {
        moving.transform2D->position.y -= movingAabb.maxY - otherAabb.minY;
      } else {
        continue;
      }

      const float bounciness = std::clamp(moving.rigidbody2D->bounciness, 0.0F, 1.0F);
      if (bounciness > 0.0F && std::abs(incomingVelocity) > 2.0F) {
        moving.rigidbody2D->velocity.y = -incomingVelocity * bounciness;
      } else {
        moving.rigidbody2D->velocity.y = 0.0F;
      }
    }
  }
}

} // namespace

std::optional<Vec2> rigidbodyVelocity(const World& world, const std::string& entityId) {
  const Entity* entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->rigidbody2D.has_value()) {
    return std::nullopt;
  }
  return entity->rigidbody2D->velocity;
}

bool setRigidbodyVelocity(World& world, const std::string& entityId, const Vec2 velocity) {
  Entity* entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->rigidbody2D.has_value()) {
    return false;
  }
  entity->rigidbody2D->velocity = velocity;
  return true;
}

bool setRigidbodyVelocityX(World& world, const std::string& entityId, const float x) {
  Entity* entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->rigidbody2D.has_value()) {
    return false;
  }
  entity->rigidbody2D->velocity.x = x;
  return true;
}

bool setRigidbodyVelocityY(World& world, const std::string& entityId, const float y) {
  Entity* entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->rigidbody2D.has_value()) {
    return false;
  }
  entity->rigidbody2D->velocity.y = y;
  return true;
}

bool addRigidbodyImpulse(World& world, const std::string& entityId, const Vec2 impulse) {
  Entity* entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->rigidbody2D.has_value()) {
    return false;
  }
  entity->rigidbody2D->velocity.x += impulse.x;
  entity->rigidbody2D->velocity.y += impulse.y;
  return true;
}

bool overlapBox(const World& world, const Vec2 center, const Vec2 size, const std::string& ignoredEntityId) {
  const Aabb query = makeAabb(center, size);
  for (const Entity& entity : world.entities) {
    if (entity.id == ignoredEntityId || !participatesInCollision(entity)) {
      continue;
    }
    if (intersects(query, colliderAabb(entity))) {
      return true;
    }
  }
  return false;
}

void stepPhysics2D(World& world, const float fixedDt, const PhysicsSettings2D& settings) {
  for (Entity& entity : world.entities) {
    if (!isDynamic(entity) || !entity.transform2D.has_value()) {
      continue;
    }

    entity.rigidbody2D->velocity.x = std::clamp(entity.rigidbody2D->velocity.x, -100.0F, 100.0F);
    entity.rigidbody2D->velocity.y += settings.gravity.y * entity.rigidbody2D->gravityScale * fixedDt;
    entity.rigidbody2D->velocity.y = std::clamp(entity.rigidbody2D->velocity.y, -100.0F, 100.0F);

    resolveAxis(world, entity, Vec2{.x = entity.rigidbody2D->velocity.x * fixedDt, .y = 0.0F}, true);
    resolveAxis(world, entity, Vec2{.x = 0.0F, .y = entity.rigidbody2D->velocity.y * fixedDt}, false);
  }
}

} // namespace demi::runtime
