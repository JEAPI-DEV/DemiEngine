#include "demi/runtime/render/backend/ImageDecoder2D.h"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <limits>
#include <string_view>

namespace demi::runtime::render {
namespace {

bool decodePpm(const std::span<const std::byte> encoded, ImageData2D &image,
               std::string &error) {
  const std::string_view text(reinterpret_cast<const char *>(encoded.data()),
                              encoded.size());
  if (!text.starts_with("P3") && !text.starts_with("P6"))
    return false;
  const bool binary = text.starts_with("P6");
  std::size_t cursor = 2;
  const auto token = [&]() -> std::string_view {
    while (cursor < text.size()) {
      if (text[cursor] == '#') {
        while (cursor < text.size() && text[cursor] != '\n')
          ++cursor;
      } else if (std::isspace(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
      } else {
        break;
      }
    }
    const std::size_t start = cursor;
    while (cursor < text.size() &&
           !std::isspace(static_cast<unsigned char>(text[cursor])) &&
           text[cursor] != '#')
      ++cursor;
    return text.substr(start, cursor - start);
  };
  const auto number = [&](int &value) {
    const std::string_view valueText = token();
    if (valueText.empty())
      return false;
    const auto result =
        std::from_chars(valueText.data(), valueText.data() + valueText.size(),
                        value);
    return result.ec == std::errc{} &&
           result.ptr == valueText.data() + valueText.size();
  };
  int width = 0;
  int height = 0;
  int maximum = 0;
  if (!number(width) || !number(height) || !number(maximum) || width <= 0 ||
      height <= 0 || width > std::numeric_limits<std::uint16_t>::max() ||
      height > std::numeric_limits<std::uint16_t>::max() || maximum <= 0 ||
      maximum > 255) {
    error = "PPM header is invalid or unsupported.";
    return false;
  }
  const std::size_t pixels =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  image.width = static_cast<std::uint16_t>(width);
  image.height = static_cast<std::uint16_t>(height);
  image.rgba.resize(pixels * 4U);
  const auto scale = [maximum](const int value) {
    return static_cast<std::byte>(
        std::clamp(value, 0, maximum) * 255 / maximum);
  };
  if (binary) {
    if (cursor >= text.size() ||
        !std::isspace(static_cast<unsigned char>(text[cursor]))) {
      error = "PPM binary header has no pixel-data separator.";
      image = {};
      return false;
    }
    ++cursor;
    if (text.size() - cursor < pixels * 3U) {
      error = "PPM binary pixel data is truncated.";
      image = {};
      return false;
    }
    for (std::size_t index = 0; index < pixels; ++index) {
      image.rgba[index * 4U] =
          scale(static_cast<unsigned char>(text[cursor + index * 3U]));
      image.rgba[index * 4U + 1U] =
          scale(static_cast<unsigned char>(text[cursor + index * 3U + 1U]));
      image.rgba[index * 4U + 2U] =
          scale(static_cast<unsigned char>(text[cursor + index * 3U + 2U]));
      image.rgba[index * 4U + 3U] = std::byte{0xff};
    }
    return true;
  }
  for (std::size_t index = 0; index < pixels; ++index) {
    int red = 0;
    int green = 0;
    int blue = 0;
    if (!number(red) || !number(green) || !number(blue)) {
      error = "PPM text pixel data is truncated or invalid.";
      image = {};
      return false;
    }
    image.rgba[index * 4U] = scale(red);
    image.rgba[index * 4U + 1U] = scale(green);
    image.rgba[index * 4U + 2U] = scale(blue);
    image.rgba[index * 4U + 3U] = std::byte{0xff};
  }
  return true;
}

} // namespace

bool decodeImage2D(const std::span<const std::byte> encoded,
                   ImageData2D &image, std::string &error) {
  image = {};
  if (encoded.empty() ||
      encoded.size() > std::numeric_limits<std::uint32_t>::max()) {
    error = "Image data is empty or too large to decode.";
    return false;
  }
  if (encoded.size() >= 2 &&
      (encoded[0] == static_cast<std::byte>('P') &&
       (encoded[1] == static_cast<std::byte>('3') ||
        encoded[1] == static_cast<std::byte>('6'))))
    return decodePpm(encoded, image, error);

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
