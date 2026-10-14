#pragma once

#include "demi/runtime/render/backend/ImageDecoder2D.h"

namespace demi::runtime::render {

// Storage stays RGBA8. Color mips average RGB in linear light and encode back
// to sRGB; normal mips average decoded vectors and normalize them again.
// Scalar maps and callers without a declared semantic use linear channels.
enum class ImageMipSemantic { Linear, Color, Normal };

// Packs the unchanged base level followed by every level down to 1x1.
// Box filtering retains every source texel, including odd image dimensions.
[[nodiscard]] std::vector<std::byte>
imageMipChain2D(const ImageData2D &image,
                ImageMipSemantic semantic = ImageMipSemantic::Linear);

} // namespace demi::runtime::render
