#include "demi/runtime/render/backend/ImageMipmaps2D.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace demi::runtime::render {
namespace {

double decodeColor(double value) {
  return value <= 0.04045 ? value / 12.92
                          : std::pow((value + 0.055) / 1.055, 2.4);
}

double encodeColor(double value) {
  return value <= 0.0031308 ? value * 12.92
                            : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

std::byte encodeChannel(double value) {
  const auto integer = std::lround(std::clamp(value, 0.0, 1.0) * 255.0);
  return static_cast<std::byte>(integer);
}

std::array<std::byte, 4> filteredTexel(const std::vector<std::byte> &source,
                                       std::size_t offset, unsigned width,
                                       unsigned x0, unsigned x1, unsigned y0,
                                       unsigned y1, ImageMipSemantic semantic) {
  std::array<double, 4> sum{};
  for (unsigned y = y0; y < y1; ++y) {
    for (unsigned x = x0; x < x1; ++x) {
      const std::size_t pixel = offset + (std::size_t(y) * width + x) * 4;
      for (std::size_t channel = 0; channel < 4; ++channel) {
        double value =
            std::to_integer<unsigned>(source[pixel + channel]) / 255.0;
        if (channel < 3 && semantic == ImageMipSemantic::Color) {
          value = decodeColor(value);
        } else if (channel < 3 && semantic == ImageMipSemantic::Normal) {
          value = 2.0 * value - 1.0;
        }
        sum[channel] += value;
      }
    }
  }
  const double count = double(x1 - x0) * (y1 - y0);
  for (auto &channel : sum) {
    channel /= count;
  }

  if (semantic == ImageMipSemantic::Normal) {
    const double length = std::hypot(sum[0], sum[1], sum[2]);
    // Opposing normals have no mean direction. A neutral tangent-space normal
    // is deterministic and does not invent a direction along the surface.
    if (length > 1e-12) {
      for (std::size_t channel = 0; channel < 3; ++channel) {
        sum[channel] = sum[channel] / length;
      }
    } else {
      sum[0] = 0;
      sum[1] = 0;
      sum[2] = 1;
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
      sum[channel] = sum[channel] * 0.5 + 0.5;
    }
  } else if (semantic == ImageMipSemantic::Color) {
    for (std::size_t channel = 0; channel < 3; ++channel) {
      sum[channel] = encodeColor(sum[channel]);
    }
  }
  return {encodeChannel(sum[0]), encodeChannel(sum[1]), encodeChannel(sum[2]),
          encodeChannel(sum[3])};
}

} // namespace

std::vector<std::byte> imageMipChain2D(const ImageData2D &image,
                                       ImageMipSemantic semantic) {
  const auto expectedBytes = std::size_t(image.width) * image.height * 4;
  if (image.width == 0 || image.height == 0 ||
      image.rgba.size() != expectedBytes) {
    return {};
  }
  std::vector<std::byte> packed = image.rgba;
  unsigned width = image.width;
  unsigned height = image.height;
  std::size_t offset = 0;
  while (width > 1 || height > 1) {
    const unsigned nextWidth = std::max(1U, width / 2);
    const unsigned nextHeight = std::max(1U, height / 2);
    const auto nextOffset = packed.size();
    packed.resize(nextOffset + std::size_t(nextWidth) * nextHeight * 4);
    for (unsigned y = 0; y < nextHeight; ++y) {
      for (unsigned x = 0; x < nextWidth; ++x) {
        const unsigned x0 = x * width / nextWidth;
        const unsigned x1 = (x + 1) * width / nextWidth;
        const unsigned y0 = y * height / nextHeight;
        const unsigned y1 = (y + 1) * height / nextHeight;
        const auto pixel =
            filteredTexel(packed, offset, width, x0, x1, y0, y1, semantic);
        const auto destination =
            nextOffset + (std::size_t(y) * nextWidth + x) * 4;
        std::copy(pixel.begin(), pixel.end(), packed.begin() + destination);
      }
    }
    offset = nextOffset;
    width = nextWidth;
    height = nextHeight;
  }
  return packed;
}
} // namespace demi::runtime::render
