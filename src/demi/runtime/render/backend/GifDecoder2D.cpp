#include "demi/runtime/render/backend/ImageDecoder2D.h"

#define STB_IMAGE_STATIC
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cstring>
#include <limits>

namespace demi::runtime::render {

bool decodeGif2D(const std::span<const std::byte> encoded,
                 AnimatedImageData2D &animation, std::string &error) {
  animation = {};
  if (encoded.empty() ||
      encoded.size() > static_cast<std::size_t>(
                           std::numeric_limits<int>::max())) {
    error = "GIF data is empty or too large to decode.";
    return false;
  }
  int *delays = nullptr;
  int width = 0;
  int height = 0;
  int frameCount = 0;
  int sourceComponents = 0;
  stbi_uc *pixels = stbi_load_gif_from_memory(
      reinterpret_cast<const stbi_uc *>(encoded.data()),
      static_cast<int>(encoded.size()), &delays, &width, &height, &frameCount,
      &sourceComponents, 4);
  if (pixels == nullptr || width <= 0 || height <= 0 || frameCount <= 0 ||
      width > std::numeric_limits<std::uint16_t>::max() ||
      height > std::numeric_limits<std::uint16_t>::max()) {
    error = stbi_failure_reason() != nullptr
                ? stbi_failure_reason()
                : "GIF data is invalid or unsupported.";
    stbi_image_free(delays);
    stbi_image_free(pixels);
    return false;
  }
  const std::size_t frameBytes =
      static_cast<std::size_t>(width) * height * 4U;
  animation.frames.reserve(static_cast<std::size_t>(frameCount));
  animation.frameDurations.reserve(static_cast<std::size_t>(frameCount));
  for (int frame = 0; frame < frameCount; ++frame) {
    ImageData2D image{
        .width = static_cast<std::uint16_t>(width),
        .height = static_cast<std::uint16_t>(height),
        .rgba = std::vector<std::byte>(frameBytes),
    };
    std::memcpy(image.rgba.data(), pixels + frameBytes * frame, frameBytes);
    animation.frames.push_back(std::move(image));
    const int milliseconds = delays != nullptr ? delays[frame] : 100;
    animation.frameDurations.push_back(
        std::max(milliseconds, 10) / 1000.0F);
  }
  stbi_image_free(delays);
  stbi_image_free(pixels);
  return true;
}

} // namespace demi::runtime::render
