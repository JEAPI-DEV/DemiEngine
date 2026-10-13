#include "demi/runtime/physics/PhysicsBodyInputs3D.h"

#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"

#include <bit>

namespace demi::runtime {
namespace {
void mix(std::uint64_t &hash, const std::uint64_t value) {
  hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
}
void mix(std::uint64_t &hash, const float value) {
  mix(hash, static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value)));
}
void mix(std::uint64_t &hash, const bool value) {
  mix(hash, static_cast<std::uint64_t>(value));
}

} // namespace

std::uint64_t physicsBodySignature3D(const World &world,
                                     const PhysicsBodyInputs3D &inputs,
                                     const WorldTransform3D &resolved) {
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  mix(hash, resolved.scale.x);
  mix(hash, resolved.scale.y);
  mix(hash, resolved.scale.z);
  if (const auto *value = inputs.box) {
    mix(hash, std::uint64_t{1});
    for (float number : {value->size.x, value->size.y, value->size.z,
                         value->offset.x, value->offset.y, value->offset.z}) {
      mix(hash, number);
    }
    mix(hash, value->isTrigger);
  } else if (const auto *value = inputs.sphere) {
    mix(hash, std::uint64_t{2});
    mix(hash, value->offset.x);
    mix(hash, value->offset.y);
    mix(hash, value->offset.z);
    mix(hash, value->radius);
    mix(hash, value->isTrigger);
  } else if (const auto *value = inputs.capsule) {
    mix(hash, std::uint64_t{3});
    mix(hash, value->offset.x);
    mix(hash, value->offset.y);
    mix(hash, value->offset.z);
    mix(hash, value->radius);
    mix(hash, value->height);
    mix(hash, value->isTrigger);
  } else if (const auto *value = inputs.convex) {
    mix(hash, std::uint64_t{4});
    mix(hash, value->offset.x);
    mix(hash, value->offset.y);
    mix(hash, value->offset.z);
    for (const Vec3 point : value->points) {
      for (float number : {point.x, point.y, point.z}) {
        mix(hash, number);
      }
    }
    mix(hash, value->isTrigger);
  } else if (const auto *value = inputs.model) {
    mix(hash, std::uint64_t{5});
    mix(hash, std::hash<std::string>{}(value->asset));
    if (value->inlineGeometry) {
      mix(hash, value->inlineGeometry->revision);
    }
    mix(hash, value->isTrigger);
    if (const auto asset = world.colliderAssets3D.find(value->asset);
        asset != world.colliderAssets3D.end()) {
      mix(hash, asset->second.revision);
    }
  }
  if (const auto *body = inputs.body) {
    mix(hash, std::hash<std::string>{}(body->bodyType));
    for (bool value :
         {body->lockPositionX, body->lockPositionY, body->lockPositionZ,
          body->lockRotationX, body->lockRotationY, body->lockRotationZ}) {
      mix(hash, value);
    }
    for (float value :
         {body->mass, body->linearDamping, body->angularDamping, body->friction,
          body->restitution, body->gravityScale}) {
      mix(hash, value);
    }
    mix(hash, body->allowSleep);
    mix(hash, static_cast<std::uint64_t>(body->solverVelocitySteps));
    mix(hash, static_cast<std::uint64_t>(body->solverPositionSteps));
  }
  return hash;
}

bool PhysicsBodyInputs3D::hasCollider() const {
  return box != nullptr || sphere != nullptr || capsule != nullptr ||
         convex != nullptr || model != nullptr;
}

namespace {

PhysicsBodyInputs3D resolveColliderInputs(PhysicsBodyInputs3D inputs) {
  const Entity &entity = inputs.entity;
  inputs.transform = entity.component<Transform3DComponent>();
  if (inputs.transform == nullptr) {
    return inputs;
  }
  inputs.model = entity.component<ModelCollider3DComponent>();
  // Preserve the native collider priority for unvalidated programmatic worlds.
  if (const auto *collider = entity.component<BoxCollider3DComponent>()) {
    inputs.box = collider;
    inputs.isTrigger = collider->isTrigger;
    inputs.layer = collider->layer;
  } else if (const auto *collider =
                 entity.component<SphereCollider3DComponent>()) {
    inputs.sphere = collider;
    inputs.isTrigger = collider->isTrigger;
    inputs.layer = collider->layer;
  } else if (const auto *collider =
                 entity.component<CapsuleCollider3DComponent>()) {
    inputs.capsule = collider;
    inputs.isTrigger = collider->isTrigger;
    inputs.layer = collider->layer;
  } else if (const auto *collider =
                 entity.component<ConvexCollider3DComponent>()) {
    inputs.convex = collider;
    inputs.isTrigger = collider->isTrigger;
    inputs.layer = collider->layer;
  } else if (inputs.model != nullptr) {
    inputs.isTrigger = inputs.model->isTrigger;
    inputs.layer = inputs.model->layer;
  }
  return inputs;
}

} // namespace

PhysicsBodyInputs3D physicsBodyInputs3D(const Entity &entity) {
  auto inputs = resolveColliderInputs({.entity = entity});
  if (inputs.hasCollider()) {
    inputs.body = entity.component<Rigidbody3DComponent>();
  }
  return inputs;
}

PhysicsBodyInputs3D physicsBodyInputs3D(Entity &entity) {
  auto inputs = resolveColliderInputs({.entity = entity});
  if (inputs.hasCollider()) {
    inputs.mutableBody = entity.component<Rigidbody3DComponent>();
    inputs.body = inputs.mutableBody;
  }
  return inputs;
}

} // namespace demi::runtime
