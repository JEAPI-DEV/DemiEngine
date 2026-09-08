#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/Box2DWorldState.h"
#include "demi/runtime/physics/PhysicsGeometry2D.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <new>
#include <unordered_set>
#include <vector>

#if DEMI_HAS_BOX2D
#include <box2d/box2d.h>
#endif

namespace demi::runtime {

using physics2d_detail::Aabb;
using physics2d_detail::absoluteScale;
using physics2d_detail::circleScale;
using physics2d_detail::colliderAabb;
using physics2d_detail::colliderLayer;
using physics2d_detail::hasCollider;
using physics2d_detail::participatesInCollision;
using physics2d_detail::scaledLocalPoint;
namespace {

constexpr float PresentationPoseEpsilon = 0.00001F;

bool samePresentationPosition(const Vec2 left, const Vec2 right) {
  return std::abs(left.x - right.x) <= PresentationPoseEpsilon &&
         std::abs(left.y - right.y) <= PresentationPoseEpsilon;
}

bool samePresentationRotation(const float left, const float right) {
  return std::abs(left - right) <= PresentationPoseEpsilon;
}

void beginPhysicsPresentationStep2D(World &world) {
  std::unordered_set<std::string> liveEntityIds;
  liveEntityIds.reserve(world.entities.size());
  for (const Entity &entity : world.entities) {
    const auto *transform = entity.component<Transform2DComponent>();
    if (!entity.enabled || transform == nullptr ||
        (!entity.hasComponent<Rigidbody2DComponent>() &&
         !hasCollider(entity))) {
      continue;
    }
    liveEntityIds.insert(entity.id);
    auto [iterator, inserted] = world.physicsPresentationPoses2D.try_emplace(
        entity.id, PhysicsPresentationPose2D{
                       .previousPosition = transform->position,
                       .currentPosition = transform->position,
                       .previousRotation = transform->rotation,
                       .currentRotation = transform->rotation,
                   });
    PhysicsPresentationPose2D &pose = iterator->second;
    const bool teleported =
        !samePresentationPosition(transform->position, pose.currentPosition) ||
        !samePresentationRotation(transform->rotation, pose.currentRotation);
    if (inserted || teleported) {
      pose.previousPosition = transform->position;
      pose.currentPosition = transform->position;
      pose.previousRotation = transform->rotation;
      pose.currentRotation = transform->rotation;
    } else {
      pose.previousPosition = pose.currentPosition;
      pose.previousRotation = pose.currentRotation;
    }
  }
  std::erase_if(world.physicsPresentationPoses2D,
                [&liveEntityIds](const auto &entry) {
                  return !liveEntityIds.contains(entry.first);
                });
}

void finishPhysicsPresentationStep2D(World &world) {
  for (const Entity &entity : world.entities) {
    const auto *transform = entity.component<Transform2DComponent>();
    if (!entity.enabled || transform == nullptr)
      continue;
    const auto found = world.physicsPresentationPoses2D.find(entity.id);
    if (found == world.physicsPresentationPoses2D.end())
      continue;
    found->second.currentPosition = transform->position;
    found->second.currentRotation = transform->rotation;
  }
}

} // namespace

namespace {

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
void stepPhysics2D(World &world, const float fixedDt,
                   const PhysicsSettings2D &settings) {
  beginPhysicsPresentationStep2D(world);
  world.previousPhysicsContacts = std::move(world.physicsContacts);
  world.physicsContacts.clear();
#if DEMI_HAS_BOX2D
  if (world.box2dState == nullptr) {
    world.box2dState = std::make_unique<Box2DWorldState>();
  }
  Box2DWorldState &state = *world.box2dState;

  if (!state.initialised) {
    state.world = new b2World({settings.gravity.x, settings.gravity.y});
    state.gravityX = settings.gravity.x;
    state.gravityY = settings.gravity.y;
    state.initialised = true;
  } else if (state.gravityX != settings.gravity.x ||
             state.gravityY != settings.gravity.y) {
    static_cast<b2World *>(state.world)
        ->SetGravity({settings.gravity.x, settings.gravity.y});
    state.gravityX = settings.gravity.x;
    state.gravityY = settings.gravity.y;
  }
  b2World &physicsWorld = *static_cast<b2World *>(state.world);

  for (void *joint : state.joints)
    physicsWorld.DestroyJoint(static_cast<b2Joint *>(joint));
  state.joints.clear();

  auto computeShapeSignature = [&](const Entity &entity) -> std::uint64_t {
    std::uint64_t signature = 0x9E3779B97F4A7C15ULL;
    auto mix = [&signature](std::uint64_t value) {
      signature ^=
          value + 0x9E3779B97F4A7C15ULL + (signature << 6) + (signature >> 2);
    };
    const auto mixFloat = [&mix](const float value) {
      mix(std::bit_cast<std::uint32_t>(value));
    };
    const auto mixFilter = [&](const std::string &layer,
                               const std::uint16_t categoryBits,
                               const std::uint16_t maskBits) {
      const auto category = world.physicsCategoryBits.find(layer);
      if (category == world.physicsCategoryBits.end()) {
        mix(categoryBits);
        mix(maskBits);
        return;
      }
      mix(category->second);
      const auto mask = world.physicsMaskBits.find(layer);
      mix(mask == world.physicsMaskBits.end() ? maskBits : mask->second);
    };
    if (const auto *rigidbody = entity.component<Rigidbody2DComponent>()) {
      mix(static_cast<std::uint64_t>(rigidbody->lockRotation));
      mixFloat(rigidbody->bounciness);
    }
    if (const auto *transform = entity.component<Transform2DComponent>()) {
      mixFloat(transform->scale.x);
      mixFloat(transform->scale.y);
    }
    if (const auto *box = entity.component<BoxCollider2DComponent>()) {
      mix(0x01);
      mix(static_cast<std::uint64_t>(box->isTrigger));
      mixFloat(box->offset.x);
      mixFloat(box->offset.y);
      mixFloat(box->size.x);
      mixFloat(box->size.y);
      mixFloat(box->friction);
      mixFloat(box->restitution);
      mixFloat(box->density);
      mixFilter(box->layer, box->categoryBits, box->maskBits);
    }
    if (const auto *circle = entity.component<CircleCollider2DComponent>()) {
      mix(0x02);
      mix(static_cast<std::uint64_t>(circle->isTrigger));
      mixFloat(circle->offset.x);
      mixFloat(circle->offset.y);
      mixFloat(circle->radius);
      mixFloat(circle->friction);
      mixFloat(circle->restitution);
      mixFloat(circle->density);
      mixFilter(circle->layer, circle->categoryBits, circle->maskBits);
    }
    if (const auto *capsule = entity.component<CapsuleCollider2DComponent>()) {
      mix(0x03);
      mix(static_cast<std::uint64_t>(capsule->isTrigger));
      mixFloat(capsule->offset.x);
      mixFloat(capsule->offset.y);
      mixFloat(capsule->size.x);
      mixFloat(capsule->size.y);
      mixFloat(capsule->friction);
      mixFloat(capsule->restitution);
      mixFloat(capsule->density);
      mixFilter(capsule->layer, capsule->categoryBits, capsule->maskBits);
    }
    if (const auto *polygon = entity.component<PolygonCollider2DComponent>()) {
      mix(0x04);
      mix(static_cast<std::uint64_t>(polygon->isTrigger));
      mixFloat(polygon->offset.x);
      mixFloat(polygon->offset.y);
      mixFloat(polygon->friction);
      mixFloat(polygon->restitution);
      mixFloat(polygon->density);
      for (const Vec2 point : polygon->points) {
        mixFloat(point.x);
        mixFloat(point.y);
      }
      mixFilter(polygon->layer, polygon->categoryBits, polygon->maskBits);
    }
    if (const auto *edge = entity.component<EdgeCollider2DComponent>()) {
      mix(0x05);
      mix(static_cast<std::uint64_t>(edge->loop));
      mix(static_cast<std::uint64_t>(edge->isTrigger));
      mixFloat(edge->friction);
      mixFloat(edge->restitution);
      mixFloat(edge->density);
      for (const Vec2 point : edge->points) {
        mixFloat(point.x);
        mixFloat(point.y);
      }
      mixFilter(edge->layer, edge->categoryBits, edge->maskBits);
    }
    return signature;
  };

  std::unordered_set<std::string> liveEntityIds;
  liveEntityIds.reserve(world.entities.size());

  auto createFixtures = [&](Entity &entity, b2Body *body) {
    const Transform2DComponent &transform =
        *entity.component<Transform2DComponent>();
    const Vec2 scale = absoluteScale(transform);
    if (entity.hasComponent<BoxCollider2DComponent>()) {
      const BoxCollider2DComponent &box =
          *entity.component<BoxCollider2DComponent>();
      b2PolygonShape shape;
      const Vec2 offset = scaledLocalPoint(transform, box.offset);
      shape.SetAsBox(box.size.x * scale.x * 0.5F, box.size.y * scale.y * 0.5F,
                     {offset.x, offset.y}, 0.0F);
      b2FixtureDef fixtureDef;
      fixtureDef.shape = &shape;
      fixtureDef.density = box.density;
      fixtureDef.friction = box.friction;
      fixtureDef.restitution =
          box.restitution > 0.0F
              ? box.restitution
              : (entity.hasComponent<Rigidbody2DComponent>()
                     ? std::clamp(
                           entity.component<Rigidbody2DComponent>()->bounciness,
                           0.0F, 1.0F)
                     : 0.0F);
      fixtureDef.isSensor = box.isTrigger;
      fixtureDef.filter.categoryBits = box.categoryBits;
      fixtureDef.filter.maskBits = box.maskBits;
      if (const auto category = world.physicsCategoryBits.find(box.layer);
          category != world.physicsCategoryBits.end()) {
        fixtureDef.filter.categoryBits = category->second;
        fixtureDef.filter.maskBits = world.physicsMaskBits.at(box.layer);
      }
      body->CreateFixture(&fixtureDef);
    }
    if (entity.hasComponent<CircleCollider2DComponent>()) {
      const CircleCollider2DComponent &collider =
          *entity.component<CircleCollider2DComponent>();
      b2CircleShape shape;
      const Vec2 offset = scaledLocalPoint(transform, collider.offset);
      shape.m_p.Set(offset.x, offset.y);
      shape.m_radius = collider.radius * circleScale(transform);
      b2FixtureDef fixtureDef;
      fixtureDef.shape = &shape;
      fixtureDef.density = collider.density;
      fixtureDef.friction = collider.friction;
      fixtureDef.restitution =
          collider.restitution > 0.0F
              ? collider.restitution
              : (entity.hasComponent<Rigidbody2DComponent>()
                     ? std::clamp(
                           entity.component<Rigidbody2DComponent>()->bounciness,
                           0.0F, 1.0F)
                     : 0.0F);
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
    if (const auto *capsule = entity.component<CapsuleCollider2DComponent>()) {
      const Vec2 size{capsule->size.x * scale.x, capsule->size.y * scale.y};
      const Vec2 offset = scaledLocalPoint(transform, capsule->offset);
      const float radius = std::max(std::min(size.x, size.y) * 0.5F, 0.001F);
      const bool vertical = size.y >= size.x;
      const float straight =
          std::max((vertical ? size.y : size.x) - radius * 2.0F, 0.0F);
      b2FixtureDef definition;
      definition.density = capsule->density;
      definition.friction = capsule->friction;
      definition.restitution = capsule->restitution;
      definition.isSensor = capsule->isTrigger;
      definition.filter.categoryBits = capsule->categoryBits;
      definition.filter.maskBits = capsule->maskBits;
      if (const auto category = world.physicsCategoryBits.find(capsule->layer);
          category != world.physicsCategoryBits.end()) {
        definition.filter.categoryBits = category->second;
        definition.filter.maskBits = world.physicsMaskBits.at(capsule->layer);
      }
      if (straight > 0.0F) {
        b2PolygonShape middle;
        middle.SetAsBox(vertical ? radius : straight * 0.5F,
                        vertical ? straight * 0.5F : radius,
                        {offset.x, offset.y}, 0.0F);
        definition.shape = &middle;
        body->CreateFixture(&definition);
      }
      for (const float side : {-1.0F, 1.0F}) {
        b2CircleShape cap;
        cap.m_radius = radius;
        cap.m_p = {offset.x + (vertical ? 0.0F : side * straight * 0.5F),
                   offset.y + (vertical ? side * straight * 0.5F : 0.0F)};
        definition.shape = &cap;
        body->CreateFixture(&definition);
      }
    }
    if (const auto *polygon = entity.component<PolygonCollider2DComponent>();
        polygon != nullptr && polygon->points.size() >= 3) {
      std::vector<b2Vec2> vertices;
      const std::size_t count =
          std::min<std::size_t>(polygon->points.size(), b2_maxPolygonVertices);
      vertices.reserve(count);
      for (std::size_t index = 0; index < count; ++index) {
        const Vec2 point = scaledLocalPoint(
            transform, {polygon->points[index].x + polygon->offset.x,
                        polygon->points[index].y + polygon->offset.y});
        vertices.push_back({point.x, point.y});
      }
      b2PolygonShape shape;
      shape.Set(vertices.data(), static_cast<int32>(vertices.size()));
      b2FixtureDef definition;
      definition.shape = &shape;
      definition.density = polygon->density;
      definition.friction = polygon->friction;
      definition.restitution = polygon->restitution;
      definition.isSensor = polygon->isTrigger;
      definition.filter.categoryBits = polygon->categoryBits;
      definition.filter.maskBits = polygon->maskBits;
      if (const auto category = world.physicsCategoryBits.find(polygon->layer);
          category != world.physicsCategoryBits.end()) {
        definition.filter.categoryBits = category->second;
        definition.filter.maskBits = world.physicsMaskBits.at(polygon->layer);
      }
      body->CreateFixture(&definition);
    }
    if (const auto *edge = entity.component<EdgeCollider2DComponent>();
        edge != nullptr && edge->points.size() >= 2) {
      std::vector<b2Vec2> vertices;
      vertices.reserve(edge->points.size());
      for (const Vec2 point : edge->points) {
        const Vec2 scaled = scaledLocalPoint(transform, point);
        vertices.push_back({scaled.x, scaled.y});
      }
      b2ChainShape shape;
      if (edge->loop && vertices.size() >= 3)
        shape.CreateLoop(vertices.data(), static_cast<int32>(vertices.size()));
      else
        shape.CreateChain(vertices.data(), static_cast<int32>(vertices.size()),
                          vertices.front(), vertices.back());
      b2FixtureDef definition;
      definition.shape = &shape;
      definition.density = edge->density;
      definition.friction = edge->friction;
      definition.restitution = edge->restitution;
      definition.isSensor = edge->isTrigger;
      definition.filter.categoryBits = edge->categoryBits;
      definition.filter.maskBits = edge->maskBits;
      if (const auto category = world.physicsCategoryBits.find(edge->layer);
          category != world.physicsCategoryBits.end()) {
        definition.filter.categoryBits = category->second;
        definition.filter.maskBits = world.physicsMaskBits.at(edge->layer);
      }
      body->CreateFixture(&definition);
    }
  };

  auto bodyTypeFor = [](const Entity &entity) {
    if (!entity.hasComponent<Rigidbody2DComponent>())
      return b2_staticBody;
    const std::string &type =
        entity.component<Rigidbody2DComponent>()->bodyType;
    if (type == "dynamic")
      return b2_dynamicBody;
    if (type == "kinematic")
      return b2_kinematicBody;
    return b2_staticBody;
  };

  for (Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    if (!entity.hasComponent<Transform2DComponent>())
      continue;
    if (!entity.hasComponent<Rigidbody2DComponent>() && !hasCollider(entity))
      continue;

    const std::string &entityId = entity.id;
    liveEntityIds.insert(entityId);

    const Transform2DComponent &transform =
        *entity.component<Transform2DComponent>();
    const int currentType = static_cast<int>(bodyTypeFor(entity));
    const std::uint64_t currentSignature = computeShapeSignature(entity);

    b2Body *body = nullptr;
    if (const auto found = state.bodies.find(entityId);
        found != state.bodies.end()) {
      const int previousType = state.bodyTypes[entityId];
      const std::uint64_t previousSignature = state.shapeSignatures[entityId];
      if (previousType == currentType &&
          previousSignature == currentSignature) {
        body = reinterpret_cast<b2Body *>(found->second);
      } else {
        physicsWorld.DestroyBody(reinterpret_cast<b2Body *>(found->second));
        state.bodies.erase(found);
        state.bodyTypes.erase(entityId);
        state.shapeSignatures.erase(entityId);
      }
    }

    if (body == nullptr) {
      b2BodyDef bodyDef;
      bodyDef.position.Set(transform.position.x, transform.position.y);
      bodyDef.angle = transform.rotation;
      bodyDef.type = static_cast<b2BodyType>(currentType);
      if (entity.hasComponent<Rigidbody2DComponent>()) {
        const Rigidbody2DComponent &rb =
            *entity.component<Rigidbody2DComponent>();
        bodyDef.linearVelocity.Set(rb.velocity.x, rb.velocity.y);
        bodyDef.angularVelocity = rb.angularVelocity;
        bodyDef.linearDamping = rb.linearDamping;
        bodyDef.angularDamping = rb.angularDamping;
        bodyDef.fixedRotation = rb.lockRotation;
        bodyDef.gravityScale = rb.gravityScale;
        bodyDef.bullet = rb.continuous;
        bodyDef.allowSleep = rb.allowSleep;
        bodyDef.awake = rb.awake;
        bodyDef.enabled = rb.bodyEnabled;
      }
      body = physicsWorld.CreateBody(&bodyDef);
      createFixtures(entity, body);
      state.bodies.emplace(entityId, body);
      state.bodyTypes.emplace(entityId, currentType);
      state.shapeSignatures.emplace(entityId, currentSignature);
    } else {
      body->SetTransform({transform.position.x, transform.position.y},
                         transform.rotation);
      if (entity.hasComponent<Rigidbody2DComponent>()) {
        const Rigidbody2DComponent &rb =
            *entity.component<Rigidbody2DComponent>();
        body->SetLinearVelocity({rb.velocity.x, rb.velocity.y});
        body->SetAngularVelocity(rb.angularVelocity);
        body->SetLinearDamping(rb.linearDamping);
        body->SetAngularDamping(rb.angularDamping);
        body->SetGravityScale(rb.gravityScale);
        body->SetBullet(rb.continuous);
        body->SetSleepingAllowed(rb.allowSleep);
        body->SetAwake(rb.awake);
        body->SetEnabled(rb.bodyEnabled);
      }
    }
  }

  for (auto iterator = state.bodies.begin(); iterator != state.bodies.end();) {
    if (liveEntityIds.contains(iterator->first)) {
      ++iterator;
      continue;
    }
    physicsWorld.DestroyBody(reinterpret_cast<b2Body *>(iterator->second));
    state.bodyTypes.erase(iterator->first);
    state.shapeSignatures.erase(iterator->first);
    iterator = state.bodies.erase(iterator);
  }

  const auto bodyForEntity = [&](const std::string &id) -> b2Body * {
    const auto found = state.bodies.find(id);
    return found == state.bodies.end()
               ? nullptr
               : reinterpret_cast<b2Body *>(found->second);
  };

  std::vector<b2DistanceJointDef> jointDefs;
  for (const Entity &entity : world.entities) {
    const auto *joint = entity.component<DistanceJoint2DComponent>();
    if (joint == nullptr)
      continue;
    b2Body *bodyA = bodyForEntity(entity.id);
    b2Body *bodyB = bodyForEntity(joint->otherEntity);
    if (bodyA == nullptr || bodyB == nullptr || bodyA == bodyB)
      continue;
    const b2Vec2 positionA = bodyA->GetPosition();
    const b2Vec2 positionB = bodyB->GetPosition();
    b2DistanceJointDef definition;
    definition.Initialize(
        bodyA, bodyB,
        {positionA.x + joint->anchor.x, positionA.y + joint->anchor.y},
        {positionB.x + joint->otherAnchor.x,
         positionB.y + joint->otherAnchor.y});
    definition.length = joint->length;
    definition.minLength = joint->length;
    definition.maxLength = joint->length;
    definition.stiffness = joint->stiffness;
    definition.damping = joint->damping;
    definition.collideConnected = joint->collideConnected;
    jointDefs.push_back(definition);
  }
  for (b2DistanceJointDef &definition : jointDefs) {
    state.joints.push_back(physicsWorld.CreateJoint(&definition));
  }
  for (const Entity &entity : world.entities) {
    const auto *joint = entity.component<Joint2DComponent>();
    if (joint == nullptr)
      continue;
    b2Body *bodyA = bodyForEntity(entity.id);
    b2Body *bodyB = bodyForEntity(joint->otherEntity);
    if (bodyA == nullptr || bodyB == nullptr || bodyA == bodyB)
      continue;
    if (joint->type == "revolute") {
      b2RevoluteJointDef definition;
      definition.bodyA = bodyA;
      definition.bodyB = bodyB;
      definition.localAnchorA = {joint->anchor.x, joint->anchor.y};
      definition.localAnchorB = {joint->otherAnchor.x, joint->otherAnchor.y};
      definition.enableLimit = joint->enableLimit;
      definition.lowerAngle = joint->lowerLimit;
      definition.upperAngle = joint->upperLimit;
      definition.enableMotor = joint->enableMotor;
      definition.motorSpeed = joint->motorSpeed;
      definition.maxMotorTorque = joint->maxMotorTorque;
      definition.collideConnected = joint->collideConnected;
      state.joints.push_back(physicsWorld.CreateJoint(&definition));
    } else if (joint->type == "prismatic") {
      b2PrismaticJointDef definition;
      definition.bodyA = bodyA;
      definition.bodyB = bodyB;
      definition.localAnchorA = {joint->anchor.x, joint->anchor.y};
      definition.localAnchorB = {joint->otherAnchor.x, joint->otherAnchor.y};
      definition.localAxisA = {joint->axis.x, joint->axis.y};
      definition.enableLimit = joint->enableLimit;
      definition.lowerTranslation = joint->lowerLimit;
      definition.upperTranslation = joint->upperLimit;
      definition.enableMotor = joint->enableMotor;
      definition.motorSpeed = joint->motorSpeed;
      definition.maxMotorForce = joint->maxMotorForce;
      definition.collideConnected = joint->collideConnected;
      state.joints.push_back(physicsWorld.CreateJoint(&definition));
    } else if (joint->type == "weld") {
      b2WeldJointDef definition;
      definition.bodyA = bodyA;
      definition.bodyB = bodyB;
      definition.localAnchorA = {joint->anchor.x, joint->anchor.y};
      definition.localAnchorB = {joint->otherAnchor.x, joint->otherAnchor.y};
      definition.referenceAngle = bodyB->GetAngle() - bodyA->GetAngle();
      definition.collideConnected = joint->collideConnected;
      state.joints.push_back(physicsWorld.CreateJoint(&definition));
    } else if (joint->type == "rope") {
      b2DistanceJointDef definition;
      definition.bodyA = bodyA;
      definition.bodyB = bodyB;
      definition.localAnchorA = {joint->anchor.x, joint->anchor.y};
      definition.localAnchorB = {joint->otherAnchor.x, joint->otherAnchor.y};
      definition.length = joint->maxLength;
      definition.minLength = 0.0F;
      definition.maxLength = joint->maxLength;
      definition.collideConnected = joint->collideConnected;
      state.joints.push_back(physicsWorld.CreateJoint(&definition));
    } else if (joint->type == "motor") {
      b2MotorJointDef definition;
      definition.Initialize(bodyA, bodyB);
      definition.linearOffset = {joint->otherAnchor.x - joint->anchor.x,
                                 joint->otherAnchor.y - joint->anchor.y};
      definition.angularOffset = joint->upperLimit;
      definition.maxForce = joint->maxMotorForce;
      definition.maxTorque = joint->maxMotorTorque;
      definition.correctionFactor = joint->correctionFactor;
      definition.collideConnected = joint->collideConnected;
      state.joints.push_back(physicsWorld.CreateJoint(&definition));
    }
  }

  physicsWorld.Step(fixedDt, 8, 3);

  for (b2Contact *contact = physicsWorld.GetContactList(); contact != nullptr;
       contact = contact->GetNext()) {
    if (!contact->IsTouching()) {
      continue;
    }

    b2Fixture *fixtureA = contact->GetFixtureA();
    b2Fixture *fixtureB = contact->GetFixtureB();
    b2Body *bodyA = fixtureA->GetBody();
    b2Body *bodyB = fixtureB->GetBody();

    auto findEntityForBody = [&](const b2Body *body) -> Entity * {
      for (Entity &candidate : world.entities) {
        const auto found = state.bodies.find(candidate.id);
        if (found != state.bodies.end() && found->second == body) {
          return &candidate;
        }
      }
      return nullptr;
    };
    Entity *entityA = findEntityForBody(bodyA);
    Entity *entityB = findEntityForBody(bodyB);
    if (entityA == nullptr || entityB == nullptr) {
      continue;
    }

    b2WorldManifold manifold;
    contact->GetWorldManifold(&manifold);
    const b2Manifold *localManifold = contact->GetManifold();
    const Vec2 point =
        localManifold->pointCount > 0
            ? Vec2{.x = manifold.points[0].x, .y = manifold.points[0].y}
            : Vec2{};
    const float normalImpulse = localManifold->pointCount > 0
                                    ? localManifold->points[0].normalImpulse
                                    : 0.0F;
    const bool trigger = fixtureA->IsSensor() || fixtureB->IsSensor();
    const std::string layerA = colliderLayer(*entityA);
    const std::string layerB = colliderLayer(*entityB);
    const auto recordContact = [&world](PhysicsContact2D value) {
      const auto duplicate = std::ranges::find_if(
          world.physicsContacts, [&value](const PhysicsContact2D &existing) {
            return existing.entityId == value.entityId &&
                   existing.otherEntityId == value.otherEntityId &&
                   existing.isTrigger == value.isTrigger;
          });
      if (duplicate == world.physicsContacts.end()) {
        world.physicsContacts.push_back(std::move(value));
      } else if (value.normalImpulse > duplicate->normalImpulse) {
        duplicate->point = value.point;
        duplicate->normal = value.normal;
        duplicate->normalImpulse = value.normalImpulse;
      }
    };
    recordContact({
        .entityId = entityA->id,
        .otherEntityId = entityB->id,
        .otherLayer = layerB,
        .point = point,
        .normal = Vec2{.x = -manifold.normal.x, .y = -manifold.normal.y},
        .normalImpulse = normalImpulse,
        .isTrigger = trigger,
    });
    recordContact({
        .entityId = entityB->id,
        .otherEntityId = entityA->id,
        .otherLayer = layerA,
        .point = point,
        .normal = Vec2{.x = manifold.normal.x, .y = manifold.normal.y},
        .normalImpulse = normalImpulse,
        .isTrigger = trigger,
    });
  }

  for (Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    if (!entity.hasComponent<Transform2DComponent>())
      continue;
    const auto found = state.bodies.find(entity.id);
    if (found == state.bodies.end())
      continue;
    b2Body *body = reinterpret_cast<b2Body *>(found->second);
    const b2Vec2 position = body->GetPosition();
    entity.component<Transform2DComponent>()->position =
        Vec2{.x = position.x, .y = position.y};
    entity.component<Transform2DComponent>()->rotation = body->GetAngle();
    if (entity.hasComponent<Rigidbody2DComponent>()) {
      const b2Vec2 velocity = body->GetLinearVelocity();
      entity.component<Rigidbody2DComponent>()->velocity =
          Vec2{.x = velocity.x, .y = velocity.y};
      entity.component<Rigidbody2DComponent>()->angularVelocity =
          body->GetAngularVelocity();
      entity.component<Rigidbody2DComponent>()->awake = body->IsAwake();
    }
  }
#else
  for (Entity &entity : world.entities) {
    if (!entity.enabled || !isDynamic(entity) ||
        !entity.hasComponent<Transform2DComponent>()) {
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

  finishPhysicsPresentationStep2D(world);

  const auto sameContact = [](const PhysicsContact2D &left,
                              const PhysicsContact2D &right) {
    return left.entityId == right.entityId &&
           left.otherEntityId == right.otherEntityId &&
           left.isTrigger == right.isTrigger;
  };
  for (PhysicsContact2D &contact : world.physicsContacts) {
    contact.phase =
        std::ranges::any_of(world.previousPhysicsContacts,
                            [&](const PhysicsContact2D &previous) {
                              return sameContact(contact, previous) &&
                                     previous.phase != "exit";
                            })
            ? "stay"
            : "enter";
  }
  for (const PhysicsContact2D &previous : world.previousPhysicsContacts) {
    if (previous.phase == "exit" ||
        std::ranges::any_of(world.physicsContacts,
                            [&](const PhysicsContact2D &current) {
                              return sameContact(previous, current);
                            }))
      continue;
    PhysicsContact2D exited = previous;
    exited.phase = "exit";
    exited.normalImpulse = 0.0F;
    world.physicsContacts.push_back(std::move(exited));
  }
}

} // namespace demi::runtime
