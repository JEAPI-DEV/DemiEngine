#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <optional>
#include <string>

namespace demi::runtime {

struct World;
struct Entity;

struct ColliderAsset3D {
  Vec3 size = {1.0F, 1.0F, 1.0F};
  Vec3 offset;
};

struct BoxColliderShape3D {
  Vec3 size;
  Vec3 offset;
  bool isTrigger = false;
};

// Resolves authored ModelCollider3D references into the immutable collider
// shapes that runtime physics and debug rendering consume.
[[nodiscard]] bool resolveColliderAssets3D(World &world,
                                           const AssetRegistry &registry,
                                           std::string &error);
[[nodiscard]] std::optional<BoxColliderShape3D>
resolvedBoxCollider3D(const World &world, const Entity &entity);

} // namespace demi::runtime
