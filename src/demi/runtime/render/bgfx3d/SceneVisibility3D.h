#pragma once

#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"

#include <cstddef>
#include <optional>
#include <memory>
#include <vector>

namespace demi::runtime {
class Entity;
class JobSystem;
struct World;
struct MeshRendererComponent;
struct MeshMaterialSections3D;
} // namespace demi::runtime

namespace demi::runtime::render {
class TerrainDrawSelection3D;
class GpuMesh3D;

struct VisibleMesh3D {
  const Entity *entity = nullptr;
  WorldTransform3D transform;
  std::optional<std::size_t> materialSection;
  // A depth-only pass may share the first section's state across the full mesh.
  bool drawAllSections = false;
  // Native terrain render variants replace geometry/sections only. Appearance
  // and gameplay collision remain on the original generated entity.
  std::shared_ptr<const MeshRendererComponent> terrainGeometry;
  const MeshMaterialSections3D *terrainSections = nullptr;
  std::size_t terrainLevel = 0;
  bool terrainSurface = false;
  const GpuMesh3D *terrainGpuMesh = nullptr;
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
                       JobSystem *jobs = nullptr,
                       TerrainDrawSelection3D *terrainSelection = nullptr,
                       const BgfxCameraFrame3D *lodCamera = nullptr);

} // namespace demi::runtime::render
