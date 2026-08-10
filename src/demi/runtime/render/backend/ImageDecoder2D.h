#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace demi::runtime::render {

struct ImageData2D {
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::vector<std::byte> rgba;
};

struct AnimatedImageData2D {
  std::vector<ImageData2D> frames;
  std::vector<float> frameDurations;
};

// Decodes common packaged image formats into the one backend-neutral format
// consumed by the 2D resource uploader.
[[nodiscard]] bool decodeImage2D(std::span<const std::byte> encoded,
                                 ImageData2D &image, std::string &error);
[[nodiscard]] bool decodeGif2D(std::span<const std::byte> encoded,
                               AnimatedImageData2D &animation,
                               std::string &error);

} // namespace demi::runtime::render
