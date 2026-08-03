#pragma once

#include "demi/runtime/scene/Transform3DHierarchy.h"

#include <array>

namespace demi::runtime::render {

// Composes an entity's resolved world transform with the authored mesh size.
// The returned matrix is column-major, matching bgfx's transform contract.
[[nodiscard]] std::array<float, 16>
composeMeshTransform3D(const WorldTransform3D &transform, Vec3 meshSize);

} // namespace demi::runtime::render
