#pragma once

#include "demi/runtime/render/backend/RenderCommands.h"
#include <array>

namespace demi::runtime::render {

// Builds the backend's column-major right-handed view matrix. Orientation is
// evaluated at the origin so a distant eye cannot round away its direction.
[[nodiscard]] std::array<float, 16> bgfxViewMatrix3D(const View3DConfig &view);

} // namespace demi::runtime::render
