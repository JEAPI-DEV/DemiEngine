#include "demi/runtime/render/backend/ImageDecoder2D.h"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <cstring>
#include <limits>

namespace demi::runtime::render {

bool decodeImage2D(const std::span<const std::byte> encoded, ImageData2D &image,
                   std::string &error) {
  image = {};
  if (encoded.empty() ||
      encoded.size() > std::numeric_limits<std::uint32_t>::max()) {
    error = "Image data is empty or too large to decode.";
    return false;
  }
  bx::DefaultAllocator allocator;
  bx::Error decodeError;
  bimg::ImageContainer *decoded = bimg::imageParse(
      &allocator, encoded.data(), static_cast<std::uint32_t>(encoded.size()),
      bimg::TextureFormat::RGBA8, &decodeError);
  if (decoded == nullptr) {
    error = decodeError.isOk() ? "The image format is invalid or unsupported."
                               : decodeError.getMessage().getCPtr();
    return false;
  }

  const bool valid =
      decoded->m_width > 0 && decoded->m_height > 0 &&
      decoded->m_width <= std::numeric_limits<std::uint16_t>::max() &&
      decoded->m_height <= std::numeric_limits<std::uint16_t>::max() &&
      decoded->m_depth == 1 && decoded->m_numLayers == 1 &&
      !decoded->m_cubeMap &&
      decoded->m_size >= decoded->m_width * decoded->m_height * 4U;
  if (!valid) {
    bimg::imageFree(decoded);
    error = "2D images must be a non-empty single-layer image up to 65535px.";
    return false;
  }

  image.width = static_cast<std::uint16_t>(decoded->m_width);
  image.height = static_cast<std::uint16_t>(decoded->m_height);
  image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4U);
  std::memcpy(image.rgba.data(), decoded->m_data, image.rgba.size());
  bimg::imageFree(decoded);
  return true;
}

} // namespace demi::runtime::render
