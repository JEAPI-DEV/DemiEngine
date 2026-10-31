#pragma once

#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"
#include "demi/runtime/render/bgfx3d/MeshBounds3D.h"
#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"
#include <array>
#include <functional>
#include <string_view>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace demi::runtime {
class Entity;
class JobSystem;
struct World;
struct MeshRendererComponent;
struct MeshMaterialSections3D;
class TerrainLodResidency;
} // namespace demi::runtime

namespace demi::runtime::render {
class TerrainDrawSelection3D;
class TerrainCameraDemand3D;
class TerrainPatchBatch3D;
class GpuMesh3D;
class MaterialLibrary;
class MeshInstancePreparation3D;
using ModelBoundsResolver3D =
    std::function<std::optional<MeshBounds3D>(std::string_view)>;

struct VisibleMesh3D {
  const Entity *entity = nullptr;
  WorldTransform3D transform;
  std::optional<std::size_t> materialSection;
  // A depth-only pass may share the first section's state across the full mesh.
  bool drawAllSections = false;
  // Native terrain render variants replace geometry/sections only. Appearance
  // and gameplay collision remain on the original generated entity.
  std::shared_ptr<const MeshRendererComponent> terrainGeometry;
  std::shared_ptr<const PreparedTerrainLodLevel> terrainPreparedLevel;
  const MeshMaterialSections3D *terrainSections = nullptr;
  // Imported model appearance belongs to its resource, not the authored entity.
  const MeshMaterialSections3D *modelSections = nullptr;
  std::size_t terrainLevel = 0;
  bool terrainSurface = false;
  const GpuMesh3D *terrainGpuMesh = nullptr;
  // Transient renderer preparation, valid through the current presentation.
  // Null for ordinary entities and callers using uncached extraction.
  const std::array<float, 16> *preparedModelMatrix = nullptr;
  const MeshInstancePreparation3D::EntitySource *preparedSource = nullptr;
};

using ModelSectionsResolver3D =
    std::function<const MeshMaterialSections3D *(const VisibleMesh3D &, Vec3)>;
// Eligible instance sources resolve their LOD/material groups once during
// compiled submission, rather than expanding sections for every visible copy.
using CompiledInstanceSource3D = std::function<bool(const VisibleMesh3D &)>;

class PreparedInstanceView3D;

struct SceneVisibility3D {
  std::vector<VisibleMesh3D> meshes;
  std::size_t considered = 0;
  std::size_t culled = 0;
  std::size_t instanceNodesTested = 0;
  std::size_t shadowHullSourcesRejected = 0;
  std::size_t shadowHullCopiesRejectedByIndex = 0;
  std::size_t shadowHullCopiesRejected = 0;
  std::size_t instanceCopiesExamined = 0;
  std::size_t instanceCopiesRejectedByIndex = 0;
  std::size_t instanceSourcesReused = 0;
  std::size_t instanceCopiesReused = 0;
  std::size_t sectionSourcesExamined = 0;
  std::size_t sectionCopiesExamined = 0;
  std::size_t sectionDrawsEmitted = 0;
};

// Extracts immutable draw inputs in World order. Bounds tests may run on the
// engine worker pool, while resource uploads and GPU commands remain on the
// render thread.
[[nodiscard]] SceneVisibility3D
extractVisibleMeshes3D(const World &world, const BgfxCameraFrame3D &frame,
                       JobSystem *jobs = nullptr,
                       TerrainDrawSelection3D *terrainSelection = nullptr,
                       const BgfxCameraFrame3D *lodCamera = nullptr,
                       TerrainLodResidency *terrainResidency = nullptr,
                       TerrainCameraDemand3D *terrainDemand = nullptr,
                       const TerrainPatchBatch3D *terrainPatches = nullptr,
                       const MaterialLibrary *materials = nullptr,
                       const ModelBoundsResolver3D &modelBounds = {},
                       MeshInstancePreparation3D *instancePreparation = nullptr,
                       PreparedInstanceView3D *preparedInstances = nullptr,
                       const ModelSectionsResolver3D &modelSections = {},
                       const CompiledInstanceSource3D &compiledInstances = {});

[[nodiscard]] bool terrainChunkVisible3D(const PreparedTerrainChunkLod &chunk,
                                         const WorldTransform3D &transform,
                                         const BgfxCameraFrame3D &frame);

// Bounds-only visibility for immutable meshes, including native water. Missing
// bounds remain conservatively visible; gameplay data is never consulted.
[[nodiscard]] bool meshBoundsVisible3D(const MeshRendererComponent &mesh,
                                       const WorldTransform3D &transform,
                                       const BgfxCameraFrame3D &frame);

} // namespace demi::runtime::render
