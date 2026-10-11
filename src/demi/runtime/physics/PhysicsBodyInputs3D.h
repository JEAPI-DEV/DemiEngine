#pragma once

#include "demi/runtime/scene/Transform3DHierarchy.h"

#include <cstdint>
#include <string_view>

namespace demi::runtime {

struct Entity;
struct World;
struct Rigidbody3DComponent;
struct BoxCollider3DComponent;
struct SphereCollider3DComponent;
struct CapsuleCollider3DComponent;
struct ConvexCollider3DComponent;
struct ModelCollider3DComponent;

// Borrows current native values for one synchronization operation. Never retain
// this view across component replacement or entity-vector mutation. Entities
// without a 3D transform or collider return an incomplete view without
// resolving body inputs.
struct PhysicsBodyInputs3D {
  const Entity &entity;
  const Transform3DComponent *transform = nullptr;
  const Rigidbody3DComponent *body = nullptr;
  Rigidbody3DComponent *mutableBody = nullptr;
  const BoxCollider3DComponent *box = nullptr;
  const SphereCollider3DComponent *sphere = nullptr;
  const CapsuleCollider3DComponent *capsule = nullptr;
  const ConvexCollider3DComponent *convex = nullptr;
  const ModelCollider3DComponent *model = nullptr;
  bool isTrigger = false;
  std::string_view layer;

  [[nodiscard]] bool hasCollider() const;
};

[[nodiscard]] PhysicsBodyInputs3D physicsBodyInputs3D(const Entity &entity);
[[nodiscard]] PhysicsBodyInputs3D physicsBodyInputs3D(Entity &entity);
[[nodiscard]] std::uint64_t
physicsBodySignature3D(const World &world, const PhysicsBodyInputs3D &inputs,
                       const WorldTransform3D &transform);
} // namespace demi::runtime
