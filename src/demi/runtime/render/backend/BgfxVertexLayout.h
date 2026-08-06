#pragma once

#include "demi/runtime/render/backend/GpuResources.h"

#include <bgfx/bgfx.h>

#include <string>

namespace demi::runtime::render {

[[nodiscard]] bool buildBgfxVertexLayout(const VertexLayout &source,
                                         bgfx::VertexLayout &result,
                                         std::string &error);

} // namespace demi::runtime::render
