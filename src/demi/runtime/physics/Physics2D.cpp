#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/Box2DShapes2D.h"
#include "demi/runtime/physics/Box2DWorldState.h"
#include "demi/runtime/physics/PhysicsGeometry2D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/Transform2DHierarchy.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <new>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <box2d/box2d.h>

namespace demi::runtime {

using physics2d_detail::colliderLayer;
using physics2d_detail::hasCollider;
namespace {

constexpr float PresentationPoseEpsilon = 0.00001F;
constexpr float PhysicsSyncEpsilon = 0.000001F;

bool changed(const float authored, const float simulated) {
  return std::abs(authored - simulated) > PhysicsSyncEpsilon;
}

bool reportsContacts(const Entity &entity) {
  const auto *body = entity.component<Rigidbody2DComponent>();
  return body == nullptr || body->reportContacts;
}

struct EntityContactKey {
  const Entity *entity = nullptr;
  const Entity *other = nullptr;
  bool trigger = false;

  bool operator==(const EntityContactKey &) const = default;
};

struct EntityContactKeyHash {
  std::size_t operator()(const EntityContactKey &key) const {
    std::size_t result = std::hash<const Entity *>{}(key.entity);
    result ^= std::hash<const Entity *>{}(key.other) + 0x9e3779b9U +
              (result << 6U) + (result >> 2U);
    result ^= std::hash<bool>{}(key.trigger) + 0x9e3779b9U + (result << 6U) +
              (result >> 2U);
    return result;
  }
};

struct ContactIdentity {
  std::string_view entityId;
  std::string_view otherEntityId;
  bool trigger = false;

  bool operator==(const ContactIdentity &) const = default;
};

struct ContactIdentityHash {
  std::size_t operator()(const ContactIdentity &key) const {
    std::size_t result = std::hash<std::string_view>{}(key.entityId);
    result ^= std::hash<std::string_view>{}(key.otherEntityId) + 0x9e3779b9U +
              (result << 6U) + (result >> 2U);
    result ^= std::hash<bool>{}(key.trigger) + 0x9e3779b9U + (result << 6U) +
              (result >> 2U);
    return result;
  }
};

ContactIdentity identityOf(const PhysicsContact2D &contact) {
  return {.entityId = contact.entityId,
          .otherEntityId = contact.otherEntityId,
          .trigger = contact.isTrigger};
}

bool samePresentationPosition(const Vec2 left, const Vec2 right) {
  return std::abs(left.x - right.x) <= PresentationPoseEpsilon &&
         std::abs(left.y - right.y) <= PresentationPoseEpsilon;
}

bool samePresentationRotation(const float left, const float right) {
  return std::abs(left - right) <= PresentationPoseEpsilon;
}

void beginPhysicsPresentationStep2D(
    World &world, std::span<const std::size_t> entityIndices) {
  std::unordered_set<std::string> liveEntityIds;
  liveEntityIds.reserve(entityIndices.size());
  for (const auto entityIndex : entityIndices) {
    const Entity &entity = world.entities[entityIndex];
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

void finishPhysicsPresentationStep2D(
    World &world, std::span<const std::size_t> entityIndices) {
  for (const auto entityIndex : entityIndices) {
    const Entity &entity = world.entities[entityIndex];
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

void stepPhysics2D(World &world, const float fixedDt,
                   const PhysicsSettings2D &settings) {
  const auto entityIndices = physics2d_detail::physicsEntityIndices2D(world);
  const bool hasParents =
      std::ranges::any_of(entityIndices, [&](std::size_t index) {
        return !world.entities[index]
                    .component<Transform2DComponent>()
                    ->parent.empty();
      });
  std::vector<std::optional<WorldTransform2D>> worldPoses;
  if (hasParents) {
    worldPoses = resolveWorldTransforms2D(world);
  }
  beginPhysicsPresentationStep2D(world, entityIndices);
  world.previousPhysicsContacts = std::move(world.physicsContacts);
  world.physicsContacts.clear();
  if (world.box2dState == nullptr) {
    world.box2dState = std::make_unique<Box2DWorldState>();
  }
  Box2DWorldState &state = *world.box2dState;

  if (!state.initialised) {
    state.world = new b2World({settings.gravity.x, settings.gravity.y});
    state.joints =
        std::make_unique<Box2DJoints2D>(*static_cast<b2World *>(state.world));
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

  auto computeShapeSignature = [&](const Entity &entity,
                                   Vec2 worldScale) -> std::uint64_t {
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
    mixFloat(worldScale.x);
    mixFloat(worldScale.y);
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
  liveEntityIds.reserve(entityIndices.size());
  std::unordered_map<const b2Body *, Entity *> entitiesByBody;
  entitiesByBody.reserve(entityIndices.size());

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

  {
    ProfileScope scope("Physics2D.sync_bodies");
    for (const auto entityIndex : entityIndices) {
      Entity &entity = world.entities[entityIndex];
      if (!entity.enabled)
        continue;
      if (!entity.hasComponent<Transform2DComponent>())
        continue;
      if (!entity.hasComponent<Rigidbody2DComponent>() && !hasCollider(entity))
        continue;

      const std::string &entityId = entity.id;

      Transform2DComponent transform =
          *entity.component<Transform2DComponent>();
      if (hasParents) {
        const auto &pose = worldPoses[entityIndex];
        if (!pose) {
          continue;
        }
        transform.position = pose->position;
        transform.rotation = pose->rotation;
        transform.scale = pose->scale;
      }
      liveEntityIds.insert(entityId);
      const int currentType = static_cast<int>(bodyTypeFor(entity));
      const std::uint64_t currentSignature =
          computeShapeSignature(entity, transform.scale);

      b2Body *body = nullptr;
      if (const auto found = state.bodies.find(entityId);
          found != state.bodies.end()) {
        const int previousType = state.bodyTypes[entityId];
        const std::uint64_t previousSignature = state.shapeSignatures[entityId];
        if (previousType == currentType &&
            previousSignature == currentSignature) {
          body = reinterpret_cast<b2Body *>(found->second);
        } else {
          state.joints->removeBody(*static_cast<b2Body *>(found->second));
          physicsWorld.DestroyBody(static_cast<b2Body *>(found->second));
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
        visitBox2DFixtures2D(world, entity, transform,
                             [&](const b2FixtureDef &definition) {
                               body->CreateFixture(&definition);
                             });
        state.bodies.emplace(entityId, body);
        state.bodyTypes.emplace(entityId, currentType);
        state.shapeSignatures.emplace(entityId, currentSignature);
      } else {
        const b2Vec2 bodyPosition = body->GetPosition();
        if (changed(transform.position.x, bodyPosition.x) ||
            changed(transform.position.y, bodyPosition.y) ||
            changed(transform.rotation, body->GetAngle()))
          body->SetTransform({transform.position.x, transform.position.y},
                             transform.rotation);
        if (entity.hasComponent<Rigidbody2DComponent>()) {
          const Rigidbody2DComponent &rb =
              *entity.component<Rigidbody2DComponent>();
          const b2Vec2 bodyVelocity = body->GetLinearVelocity();
          if (changed(rb.velocity.x, bodyVelocity.x) ||
              changed(rb.velocity.y, bodyVelocity.y))
            body->SetLinearVelocity({rb.velocity.x, rb.velocity.y});
          if (changed(rb.angularVelocity, body->GetAngularVelocity()))
            body->SetAngularVelocity(rb.angularVelocity);
          if (changed(rb.linearDamping, body->GetLinearDamping()))
            body->SetLinearDamping(rb.linearDamping);
          if (changed(rb.angularDamping, body->GetAngularDamping()))
            body->SetAngularDamping(rb.angularDamping);
          if (changed(rb.gravityScale, body->GetGravityScale()))
            body->SetGravityScale(rb.gravityScale);
          if (rb.continuous != body->IsBullet())
            body->SetBullet(rb.continuous);
          if (rb.allowSleep != body->IsSleepingAllowed())
            body->SetSleepingAllowed(rb.allowSleep);
          if (rb.awake != body->IsAwake())
            body->SetAwake(rb.awake);
          if (rb.bodyEnabled != body->IsEnabled())
            body->SetEnabled(rb.bodyEnabled);
        }
      }
      entitiesByBody.emplace(body, &entity);
    }
  }

  {
    ProfileScope scope("Physics2D.remove_bodies");
    for (auto iterator = state.bodies.begin();
         iterator != state.bodies.end();) {
      if (liveEntityIds.contains(iterator->first)) {
        ++iterator;
        continue;
      }
      state.joints->removeBody(*static_cast<b2Body *>(iterator->second));
      physicsWorld.DestroyBody(static_cast<b2Body *>(iterator->second));
      state.bodyTypes.erase(iterator->first);
      state.shapeSignatures.erase(iterator->first);
      iterator = state.bodies.erase(iterator);
    }
  }

  {
    ProfileScope scope("Physics2D.sync_joints");
    state.joints->synchronize(world, entityIndices, state.bodies);
  }

  {
    ProfileScope scope("Physics2D.simulate");
    physicsWorld.Step(fixedDt, 8, 3);
  }

  {
    ProfileScope scope("Physics2D.collect_contacts");
    std::unordered_map<EntityContactKey, std::size_t, EntityContactKeyHash>
        contactIndices;
    contactIndices.reserve(
        static_cast<std::size_t>(physicsWorld.GetContactCount()) * 2U);
    for (b2Contact *contact = physicsWorld.GetContactList(); contact != nullptr;
         contact = contact->GetNext()) {
      if (!contact->IsTouching()) {
        continue;
      }

      b2Fixture *fixtureA = contact->GetFixtureA();
      b2Fixture *fixtureB = contact->GetFixtureB();
      b2Body *bodyA = fixtureA->GetBody();
      b2Body *bodyB = fixtureB->GetBody();

      const auto foundA = entitiesByBody.find(bodyA);
      const auto foundB = entitiesByBody.find(bodyB);
      Entity *entityA =
          foundA == entitiesByBody.end() ? nullptr : foundA->second;
      Entity *entityB =
          foundB == entitiesByBody.end() ? nullptr : foundB->second;
      if (entityA == nullptr || entityB == nullptr) {
        continue;
      }
      if (!reportsContacts(*entityA) && !reportsContacts(*entityB))
        continue;

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
      const auto recordContact = [&](const Entity *entity, const Entity *other,
                                     PhysicsContact2D value) {
        const EntityContactKey key{
            .entity = entity, .other = other, .trigger = value.isTrigger};
        const auto duplicate = contactIndices.find(key);
        if (duplicate == contactIndices.end()) {
          contactIndices.emplace(key, world.physicsContacts.size());
          world.physicsContacts.push_back(std::move(value));
        } else if (PhysicsContact2D &existing =
                       world.physicsContacts[duplicate->second];
                   value.normalImpulse > existing.normalImpulse) {
          existing.point = value.point;
          existing.normal = value.normal;
          existing.normalImpulse = value.normalImpulse;
        }
      };
      recordContact(
          entityA, entityB,
          {
              .entityId = entityA->id,
              .otherEntityId = entityB->id,
              .otherLayer = layerB,
              .point = point,
              .normal = Vec2{.x = -manifold.normal.x, .y = -manifold.normal.y},
              .normalImpulse = normalImpulse,
              .isTrigger = trigger,
          });
      recordContact(
          entityB, entityA,
          {
              .entityId = entityB->id,
              .otherEntityId = entityA->id,
              .otherLayer = layerA,
              .point = point,
              .normal = Vec2{.x = manifold.normal.x, .y = manifold.normal.y},
              .normalImpulse = normalImpulse,
              .isTrigger = trigger,
          });
    }
    RuntimeProfiler::setGauge("Physics2D.contacts",
                              world.physicsContacts.size());
  }

  {
    ProfileScope scope("Physics2D.sync_components");
    std::vector<std::optional<WorldTransform2D>> nativeOverrides;
    std::vector<std::optional<WorldTransform2D>> publishedPoses;
    std::unordered_map<std::string, std::size_t> parentIndices;
    if (hasParents) {
      nativeOverrides.resize(world.entities.size());
      parentIndices.reserve(world.entities.size());
      for (std::size_t index = 0; index < world.entities.size(); ++index) {
        parentIndices.emplace(world.entities[index].id, index);
      }
      for (const auto index : entityIndices) {
        const auto found = state.bodies.find(world.entities[index].id);
        if (found == state.bodies.end()) {
          continue;
        }
        const auto &body = *static_cast<b2Body *>(found->second);
        if (body.GetType() == b2_staticBody || !worldPoses[index]) {
          continue;
        }
        auto pose = *worldPoses[index];
        pose.position = {body.GetPosition().x, body.GetPosition().y};
        pose.rotation = body.GetAngle();
        nativeOverrides[index] = pose;
      }
      publishedPoses = resolveWorldTransforms2D(world, nativeOverrides);
    }
    for (const auto index : entityIndices) {
      Entity &entity = world.entities[index];
      const auto found = state.bodies.find(entity.id);
      if (!entity.enabled || found == state.bodies.end()) {
        continue;
      }
      const auto &body = *static_cast<b2Body *>(found->second);
      if (body.GetType() != b2_staticBody) {
        const auto &authored = *entity.component<Transform2DComponent>();
        WorldTransform2D pose{
            .position = {body.GetPosition().x, body.GetPosition().y},
            .rotation = body.GetAngle()};
        std::optional<WorldTransform2D> parentPose;
        if (hasParents) {
          const auto parent = parentIndices.find(authored.parent);
          if (parent != parentIndices.end()) {
            parentPose = publishedPoses[parent->second];
          }
        }
        const auto local = localTransform2D(authored, pose, parentPose);
        if (changed(authored.position.x, local.position.x) ||
            changed(authored.position.y, local.position.y) ||
            changed(authored.rotation, local.rotation)) {
          entity.updateComponent<Transform2DComponent>([&](auto &transform) {
            transform.position = local.position;
            transform.rotation = local.rotation;
          });
        }
      }
      if (auto *rigidbody = entity.component<Rigidbody2DComponent>()) {
        rigidbody->velocity = {body.GetLinearVelocity().x,
                               body.GetLinearVelocity().y};
        rigidbody->angularVelocity = body.GetAngularVelocity();
        rigidbody->awake = body.IsAwake();
      }
    }
  }

  finishPhysicsPresentationStep2D(world, entityIndices);

  {
    ProfileScope scope("Physics2D.contact_phases");
    std::unordered_set<ContactIdentity, ContactIdentityHash> previousContacts;
    previousContacts.reserve(world.previousPhysicsContacts.size());
    for (const PhysicsContact2D &previous : world.previousPhysicsContacts)
      if (previous.phase != "exit")
        previousContacts.insert(identityOf(previous));

    for (PhysicsContact2D &contact : world.physicsContacts)
      contact.phase =
          previousContacts.contains(identityOf(contact)) ? "stay" : "enter";

    std::unordered_set<ContactIdentity, ContactIdentityHash> currentContacts;
    currentContacts.reserve(world.physicsContacts.size());
    for (const PhysicsContact2D &current : world.physicsContacts)
      currentContacts.insert(identityOf(current));

    std::vector<PhysicsContact2D> exits;
    exits.reserve(world.previousPhysicsContacts.size());
    for (const PhysicsContact2D &previous : world.previousPhysicsContacts) {
      if (previous.phase == "exit" ||
          currentContacts.contains(identityOf(previous)))
        continue;
      PhysicsContact2D exited = previous;
      exited.phase = "exit";
      exited.normalImpulse = 0.0F;
      exits.push_back(std::move(exited));
    }
    world.physicsContacts.insert(world.physicsContacts.end(),
                                 std::make_move_iterator(exits.begin()),
                                 std::make_move_iterator(exits.end()));
  }
}

} // namespace demi::runtime
