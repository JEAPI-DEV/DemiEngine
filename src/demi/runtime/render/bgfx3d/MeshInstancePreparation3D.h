#pragma once

#include "demi/runtime/render/bgfx3d/MeshBounds3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include <array>
#include <optional>
#include <unordered_map>
#include <vector>

namespace demi::runtime {
struct Entity;
struct World;
struct MeshInstances3DComponent;
} // namespace demi::runtime

namespace demi::runtime::render {

// Renderer-owned, presentation-local preparation. Worlds are immutable while
// their camera/shadow/depth views render; reset before the next presentation.
// Nothing here survives to infer whether an authored component changed later.
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
    std::vector<Copy> copies;
    bool geometryPrepared = false;
    std::optional<MeshBounds3D> localBounds;
    Vec3 localCenter;
    Vec3 localExtent;
    float windExpansion = 0;
  };

  void reset();
  Source &source(const World &world, const Entity &entity,
                 const WorldTransform3D &owner,
                 const MeshInstances3DComponent &instances);

private:
  std::unordered_map<const World *, std::unordered_map<const Entity *, Source>>
      sources_;
};
} // namespace demi::runtime::render
