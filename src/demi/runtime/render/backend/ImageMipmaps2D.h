#pragma once

#include "demi/runtime/render/backend/ImageDecoder2D.h"
#include <stop_token>
#include <optional>

namespace demi::runtime::render {

// Storage stays RGBA8. Color mips average RGB in linear light and encode back
// to sRGB; normal mips average decoded vectors and normalize them again.
// Scalar maps and callers without a declared semantic use linear channels.
enum class ImageMipSemantic { Linear, Color, Normal };

// Packs the unchanged base level followed by every level down to 1x1.
// Box filtering retains every source texel, including odd image dimensions.
// An optional cutout threshold enables associated-alpha color filtering and
// coverage preservation, within the quantization of each mip. Base is unchanged.
[[nodiscard]] std::vector<std::byte>
imageMipChain2D(const ImageData2D &image,
                ImageMipSemantic semantic = ImageMipSemantic::Linear,
                std::stop_token stop = {},
                std::optional<float> alphaCoverageCutoff = {});

} // namespace demi::runtime::render
