#pragma once
#include "demi/runtime/render/backend/ImageDecoder2D.h"

namespace demi::runtime::render {
// Validated RGBA8 input; packs base level followed by every level down to 1x1.
// Box filtering retains all texels for odd dimensions. Channels remain in the
// source encoding, matching the existing RGBA8 texture sampling contract.
[[nodiscard]] std::vector<std::byte> imageMipChain2D(const ImageData2D &image);
} // namespace demi::runtime::render
