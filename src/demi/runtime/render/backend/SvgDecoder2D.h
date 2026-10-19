#pragma once

#include "demi/runtime/render/backend/ImageDecoder2D.h"

#include <filesystem>
#include <string>

namespace demi::runtime::render {

// Rasterizes an SVG asset into straight-alpha RGBA pixels. Icon assets can
// request a white mask on a 256-square canvas so UI tinting remains data
// driven. Ordinary SVG textures preserve intrinsic pixel dimensions (rounded
// up). ViewBox-only sources use a 256px longest edge with their authored
// aspect.
[[nodiscard]] bool decodeSvg2D(const std::filesystem::path &path,
                               bool monochrome, ImageData2D &image,
                               std::string &error);

} // namespace demi::runtime::render
