#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace demi::runtime::render {

struct PrimitiveMeshData3D {
  std::vector<Vec3> positions;
  std::vector<Vec2> textureCoordinates;
  std::vector<std::uint32_t> indices;
};

// Produces unit geometry centered on the origin. Entity and authored mesh
// dimensions remain draw-time transform inputs.
[[nodiscard]] bool createPrimitiveMesh3D(std::string_view shape,
                                         PrimitiveMeshData3D &output);

} // namespace demi::runtime::render
