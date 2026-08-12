#pragma once

#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"

#include <cstddef>
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
