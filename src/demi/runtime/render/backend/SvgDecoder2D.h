#pragma once

#include "demi/runtime/render/backend/ImageDecoder2D.h"

#include <filesystem>
#include <string>

namespace demi::runtime::render {

// Rasterizes an SVG asset into straight-alpha RGBA pixels. Icon assets can
// request a white mask so UI tinting remains data driven.
[[nodiscard]] bool decodeSvg2D(const std::filesystem::path &path,
                               bool monochrome, ImageData2D &image,
                               std::string &error);

} // namespace demi::runtime::render
