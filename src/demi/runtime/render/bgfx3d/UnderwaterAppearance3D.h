#pragma once

#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <array>

namespace demi::runtime::render {

// Camera-local optical state selected through the shared gameplay water query.
// An absent frame value means no underwater processing, including above water.
struct UnderwaterAppearance3D {
  Color color;
  float absorptionDistance = 3.F;
  float immersion = 0.F;
  Vec3 surfaceRelative;
  Vec3 normal{0, 1, 0};
  // Columns transform world ray directions into terrain-local units.
  std::array<Vec3, 3> inverseBasis;
  // Filled by the camera capture owner after the opaque depth pass.
  TextureHandle linearDepth;
};

} // namespace demi::runtime::render
