#pragma once

#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace demi::runtime {
class Entity;
class JobSystem;
struct World;
} // namespace demi::runtime

namespace demi::runtime::render {

struct VisibleMesh3D {
  const Entity *entity = nullptr;
  WorldTransform3D transform;
  std::optional<std::size_t> materialSection;
  // A depth-only pass may share the first section's state across the full mesh.
  bool drawAllSections = false;
};

struct SceneVisibility3D {
  std::vector<VisibleMesh3D> meshes;
  std::size_t considered = 0;
  std::size_t culled = 0;
};

// Extracts immutable draw inputs in World order. Bounds tests may run on the
// engine worker pool, while resource uploads and GPU commands remain on the
// render thread.
[[nodiscard]] SceneVisibility3D
extractVisibleMeshes3D(const World &world, const BgfxCameraFrame3D &frame,
                       JobSystem *jobs = nullptr);

} // namespace demi::runtime::render
