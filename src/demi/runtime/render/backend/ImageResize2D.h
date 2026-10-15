#pragma once

#include "demi/runtime/render/backend/ImageMipmaps2D.h"

namespace demi::runtime::render {

// Semantic area filtering for reduction, linear interpolation for enlargement.
// Invalid input or cancellation returns an empty image.
[[nodiscard]] ImageData2D
resizeImage2D(const ImageData2D &source, std::uint16_t width,
              std::uint16_t height,
              ImageMipSemantic semantic = ImageMipSemantic::Linear,
              std::stop_token stop = {});

} // namespace demi::runtime::render
