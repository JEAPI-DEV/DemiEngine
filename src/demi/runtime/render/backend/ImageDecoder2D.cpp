#include "demi/runtime/render/backend/ImageDecoder2D.h"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <lodepng.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace demi::runtime::render {
namespace {

constexpr std::array<std::byte, 8> PngSignature{
    std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
    std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};

bool isPng(const std::span<const std::byte> encoded) {
  return encoded.size() >= PngSignature.size() &&
         std::equal(PngSignature.begin(), PngSignature.end(), encoded.begin());
}

bool decodePng(const std::span<const std::byte> encoded, ImageData2D &image,
               std::string &error) {
  unsigned char *pixels = nullptr;
  unsigned int width = 0;
  unsigned int height = 0;
  const unsigned int result = lodepng_decode32(
      &pixels, &width, &height,
      reinterpret_cast<const unsigned char *>(encoded.data()), encoded.size());
  if (result != 0) {
    error = std::string("PNG decoding failed: ") + lodepng_error_text(result);
    return false;
  }

  const bool valid =
      width > 0 && height > 0 &&
      width <= std::numeric_limits<std::uint16_t>::max() &&
      height <= std::numeric_limits<std::uint16_t>::max();
  if (!valid) {
    std::free(pixels);
    error = "2D images must be a non-empty image up to 65535px.";
    return false;
  }

  image.width = static_cast<std::uint16_t>(width);
  image.height = static_cast<std::uint16_t>(height);
  image.rgba.resize(static_cast<std::size_t>(width) * height * 4U);
  std::memcpy(image.rgba.data(), pixels, image.rgba.size());
  std::free(pixels);
  return true;
}

} // namespace

bool decodeImage2D(const std::span<const std::byte> encoded, ImageData2D &image,
                   std::string &error) {
  image = {};
  if (encoded.empty() ||
      encoded.size() > std::numeric_limits<std::uint32_t>::max()) {
    error = "Image data is empty or too large to decode.";
    return false;
  }
  // bimg's indexed-PNG expansion processes sub-byte pixels in whole packed
  // groups. Narrow, valid palette images (for example a 2x1 tile palette)
  // therefore decode as zero-filled RGBA. Decode PNGs directly to an explicit
  // RGBA8 contract; bimg remains responsible for the other image formats.
  if (isPng(encoded))
    return decodePng(encoded, image, error);

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
