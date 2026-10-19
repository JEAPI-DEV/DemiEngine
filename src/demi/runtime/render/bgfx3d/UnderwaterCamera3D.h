#pragma once

#include "demi/runtime/render/bgfx3d/UnderwaterAppearance3D.h"
#include "demi/runtime/scene/EntityLookup.h"
#include "demi/runtime/terrain/TerrainWaterRuntime.h"

#include <optional>

namespace demi::runtime::render {

class UnderwaterCamera3D {
public:
  [[nodiscard]] std::optional<UnderwaterAppearance3D>
  sample(const World &world, Vec3 cameraPosition);

private:
  TerrainWaterRuntime water_;
  EntityLookup owners_;
};

} // namespace demi::runtime::render
