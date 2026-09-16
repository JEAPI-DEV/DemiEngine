#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/ColliderFractureGraph.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

struct World;
struct Entity;

struct TriangleCollider3D {
  Vec3 a;
  Vec3 b;
  Vec3 c;
};

struct ColliderPart3D {
  std::string id;
  std::vector<Vec3> points;
};

struct ColliderAsset3D {
  Vec3 size = {1.0F, 1.0F, 1.0F};
  Vec3 offset;
  float detail = 0.0F;
  std::vector<TriangleCollider3D> triangles;
  std::vector<Vec3> points;
  std::uint64_t revision = 0;
  bool resident = true;
  std::uint64_t lastUsedEpoch = 0;
  std::vector<ColliderPart3D> parts{};
  std::optional<assets::ColliderFractureGraph> fracture;
};

struct BoxColliderShape3D {
  Vec3 size;
  Vec3 offset;
  bool isTrigger = false;
};

// Resolves authored ModelCollider3D references into the immutable collider
// shapes that runtime physics and debug rendering consume.
[[nodiscard]] std::optional<ColliderAsset3D>
loadColliderAsset3D(const AssetManifest &asset, std::string &error);
[[nodiscard]] bool resolveColliderAssets3D(World &world,
                                           const AssetRegistry &registry,
                                           std::string &error);
[[nodiscard]] std::optional<BoxColliderShape3D>
resolvedBoxCollider3D(const World &world, const Entity &entity);
[[nodiscard]] const std::vector<TriangleCollider3D> *
resolvedTriangleCollider3D(const World &world, const Entity &entity);
[[nodiscard]] const std::vector<Vec3> *
resolvedConvexCollider3D(const World &world, const Entity &entity);
[[nodiscard]] const std::vector<ColliderPart3D> *
resolvedCompoundCollider3D(const World &world, const Entity &entity);

} // namespace demi::runtime
