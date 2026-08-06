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

// Extracts world-space debug geometry from the same authored collider data and
// resolved transforms used by PhysicsWorld3D. Keeping extraction separate from
// GPU submission makes shape/transform regressions testable without a backend.
[[nodiscard]] std::vector<DebugLine3D> buildDebugGeometry3D(const World &world);

[[nodiscard]] bool appendDebugGeometry3D(const World &world,
                                         PrimitiveCanvas3D &canvas);

} // namespace demi::runtime::render
