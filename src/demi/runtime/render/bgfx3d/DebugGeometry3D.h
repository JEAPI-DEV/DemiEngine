#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <vector>

namespace demi::runtime {
struct World;
}

namespace demi::runtime::render {

class PrimitiveCanvas3D;

struct DebugLine3D {
  Vec3 start;
  Vec3 end;
  Color color;
};

struct DebugGeometry3DRequest {
  bool forceColliders = false;
  bool bounds = false;
  bool lights = false;
  bool cameras = false;
};

// Extracts world-space debug geometry from the same authored collider data and
// resolved transforms used by PhysicsWorld3D. Keeping extraction separate from
// GPU submission makes shape/transform regressions testable without a backend.
[[nodiscard]] std::vector<DebugLine3D>
buildDebugGeometry3D(const World &world, DebugGeometry3DRequest request = {});

[[nodiscard]] bool appendDebugGeometry3D(const World &world,
                                         PrimitiveCanvas3D &canvas,
                                         DebugGeometry3DRequest request = {});

} // namespace demi::runtime::render
