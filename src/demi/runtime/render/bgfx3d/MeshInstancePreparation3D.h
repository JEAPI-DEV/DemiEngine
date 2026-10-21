#pragma once

#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/render/bgfx3d/InstanceSpatialIndex3D.h"
#include "demi/runtime/render/bgfx3d/MeshBounds3D.h"
#include "demi/runtime/scene/EntityComponentQuery.h"
#include "demi/runtime/scene/WorldTransformSnapshot3D.h"
#include <array>
#include <optional>
#include <unordered_map>
#include <vector>

namespace demi::runtime {
struct Entity;
struct World;
struct MeshInstances3DComponent;
struct MeshRendererComponent;
struct MeshMaterialSections3D;
struct TerrainRenderLod;
struct AnimationPlayer3DComponent;
struct FragmentOpacity3D;
struct SurfaceRelief3DComponent;
struct Dentable3DComponent;
} // namespace demi::runtime

namespace demi::runtime::render {

// Renderer-owned, presentation-local preparation. Worlds are immutable while
// their camera/shadow/depth views render; reset before the next presentation.
// Component addresses are retained behind storage-epoch validation. Matrices
// and plain primitive bounds reuse checked inputs; asset-dependent geometry and
// live surface classification are refreshed each presentation.
class MeshInstancePreparation3D {
public:
  struct Sphere {
    Vec3 center;
    float radius = 0;
  };
  struct Copy {
    WorldTransform3D local;
    Vec3 position;
    // Expensive transforms/bounds are lazy: distant copies pay only the cheap
    // position test, even when they never enter any view this presentation.
    std::optional<WorldTransform3D> world;
    std::optional<Sphere> bounds;
    std::optional<std::array<float, 16>> modelMatrix;
  };
  struct Source {
    WorldTransform3D owner;
    const MeshInstances3DComponent *component = nullptr;
    std::uint64_t contentRevision = 0;
    std::vector<Copy> copies;
    InstanceSpatialIndex3D spatialIndex;
    std::vector<std::size_t> visibleCandidates;
    std::vector<std::uint64_t> visibilityWords;
    bool visited = false;
    bool geometryPrepared = false;
    std::optional<Vec3> meshSize;
    std::optional<MeshBounds3D> localBounds;
    Vec3 localCenter;
    Vec3 localExtent;
    float windExpansion = 0;
  };

  struct EntitySource {
    struct SectionTransparency {
      const MeshMaterialSections3D *sections = nullptr;
      std::vector<std::optional<bool>> transparent;
    };
    const Entity *entity = nullptr;
    const MeshRendererComponent *mesh = nullptr;
    const MeshInstances3DComponent *instances = nullptr;
    const MeshMaterialSections3D *sections = nullptr;
    const TerrainRenderLod *terrain = nullptr;
    const AnimationPlayer3DComponent *animation = nullptr;
    const FragmentOpacity3D *fade = nullptr;
    const SurfaceRelief3DComponent *relief = nullptr;
    const Dentable3DComponent *dentable = nullptr;
    std::span<const MeshDent3D> dents;
    bool plainPrimitive = false;
    std::optional<WorldTransform3D> transform;
    bool transformUnchanged = false;
    Vec3 preparedMeshSize;
    MeshBounds3D primitiveBounds;
    bool geometryPrepared = false;
    bool bounded = false;
    Sphere bounds;
    std::optional<std::array<float, 16>> modelMatrix;
    // Surface classification is camera-independent. These values expire with
    // this presentation's source pointers; terrain LODs have distinct sections.
    mutable std::optional<bool> transparent;
    mutable std::vector<SectionTransparency> sectionTransparency;
  };

  // Immutable source preparation is shared by camera, shadow and depth views.
  // The renderer resets it before the next presentation, including live edits.
  std::vector<EntitySource> &worldSources(const World &world,
                                          JobSystem *jobs = nullptr);
  bool hasAnimations(const World &world);

  void reset();
  Source &source(const World &world, const Entity &entity,
                 const WorldTransform3D &owner,
                 const MeshInstances3DComponent &instances);

private:
  struct WorldSources {
    std::vector<EntitySource> entries;
    const Entity *entityData = nullptr;
    std::uint64_t storageEpoch = 0;
    bool prepared = false;
    bool visited = false;
  };
  WorldTransformSnapshot3D transformSnapshot_;
  EntityComponentQuery sourceMembership_;
  // Addresses survive only unchanged storage. Live values and derived geometry
  // are refreshed each presentation, including edits through native pointers.
  std::unordered_map<const World *, WorldSources> worldSources_;
  std::unordered_map<const World *, bool> animations_;
  std::unordered_map<const World *, std::unordered_map<const Entity *, Source>>
      sources_;
};
} // namespace demi::runtime::render
