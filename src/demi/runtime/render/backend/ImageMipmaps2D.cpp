#include "demi/runtime/render/backend/ImageMipmaps2D.h"

#include "demi/runtime/render/backend/ImageColorSpace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace demi::runtime::render {
namespace {

std::byte encodeChannel(double value) {
  const auto integer = std::lround(std::clamp(value, 0.0, 1.0) * 255.0);
  return static_cast<std::byte>(integer);
}

std::array<std::byte, 4> filteredTexel(const std::vector<std::byte> &source,
                                       std::size_t offset, unsigned width,
                                       unsigned x0, unsigned x1, unsigned y0,
                                       unsigned y1, ImageMipSemantic semantic,
                                       bool associatedAlpha) {
  std::array<double, 4> sum{};
  for (unsigned y = y0; y < y1; ++y) {
    for (unsigned x = x0; x < x1; ++x) {
      const std::size_t pixel = offset + (std::size_t(y) * width + x) * 4;
      for (std::size_t channel = 0; channel < 4; ++channel) {
        double value =
            std::to_integer<unsigned>(source[pixel + channel]) / 255.0;
        if (channel < 3 && semantic == ImageMipSemantic::Color) {
          value = decodeSrgbChannel(value);
          if (associatedAlpha) {
            value *= std::to_integer<unsigned>(source[pixel + 3]) / 255.0;
          }
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
      if (associatedAlpha) {
        sum[channel] = sum[3] > 0 ? sum[channel] / sum[3] : 0;
      }
      sum[channel] = encodeSrgbChannel(sum[channel]);
    }
  }
  return {encodeChannel(sum[0]), encodeChannel(sum[1]), encodeChannel(sum[2]),
          encodeChannel(sum[3])};
}

using AlphaHistogram = std::array<std::size_t, 256>;
AlphaHistogram alphaHistogram(std::span<const std::byte> pixels) {
  AlphaHistogram histogram{};
  for (std::size_t i = 3; i < pixels.size(); i += 4) {
    ++histogram[std::to_integer<unsigned>(pixels[i])];
  }
  return histogram;
}
void preserveCoverage(std::span<std::byte> pixels, double coverage,
                      unsigned cutoff) {
  const auto histogram = alphaHistogram(pixels);
  const double target = coverage * (pixels.size() / 4);
  std::size_t current = 0;
  for (unsigned alpha = cutoff; alpha < 256; ++alpha) {
    current += histogram[alpha];
  }
  double bestError = std::abs(double(current) - target);
  double bestScale = 1;
  std::size_t covered = 0;
  for (unsigned alpha = 255; alpha > 0; --alpha) {
    covered += histogram[alpha];
    // Account for byte rounding: this bin passes, the preceding bin does not.
    const double scale = (cutoff - .499) / alpha;
    const double error = std::abs(double(covered) - target);
    if (error < bestError ||
        (error == bestError && std::abs(scale - 1) < std::abs(bestScale - 1))) {
      bestError = error;
      bestScale = scale;
    }
  }
  if (bestScale == 1) {
    return;
  }
  for (std::size_t i = 3; i < pixels.size(); i += 4) {
    pixels[i] = std::byte(std::lround(
        std::min(255.0, std::to_integer<unsigned>(pixels[i]) * bestScale)));
  }
}

} // namespace

std::vector<std::byte>
imageMipChain2D(const ImageData2D &image, ImageMipSemantic semantic,
                std::stop_token stop,
                std::optional<float> alphaCoverageCutoff) {
  if (stop.stop_requested() ||
      (alphaCoverageCutoff &&
       (semantic != ImageMipSemantic::Color ||
        !std::isfinite(*alphaCoverageCutoff) || *alphaCoverageCutoff <= 0 ||
        *alphaCoverageCutoff > 1))) {
    return {};
  }
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
      if (stop.stop_requested()) {
        return {};
      }
      for (unsigned x = 0; x < nextWidth; ++x) {
        const unsigned x0 = x * width / nextWidth;
        const unsigned x1 = (x + 1) * width / nextWidth;
        const unsigned y0 = y * height / nextHeight;
        const unsigned y1 = (y + 1) * height / nextHeight;
        const auto pixel =
            filteredTexel(packed, offset, width, x0, x1, y0, y1, semantic,
                          alphaCoverageCutoff.has_value());
        const auto destination =
            nextOffset + (std::size_t(y) * nextWidth + x) * 4;
        std::copy(pixel.begin(), pixel.end(), packed.begin() + destination);
      }
    }
    offset = nextOffset;
    width = nextWidth;
    height = nextHeight;
  }
  if (alphaCoverageCutoff) {
    const unsigned threshold =
        unsigned(std::ceil(*alphaCoverageCutoff * 255.F));
    const auto histogram = alphaHistogram(image.rgba);
    std::size_t covered = 0;
    for (unsigned alpha = threshold; alpha < 256; ++alpha) {
      covered += histogram[alpha];
    }
    const double coverage =
        double(covered) / (std::size_t(image.width) * image.height);
    width = image.width;
    height = image.height;
    offset = expectedBytes;
    // Apply after building the entire raw chain so alpha boosts do not
    // compound.
    while (width > 1 || height > 1) {
      if (stop.stop_requested()) {
        return {};
      }
      width = std::max(1U, width / 2);
      height = std::max(1U, height / 2);
      const auto bytes = std::size_t(width) * height * 4;
      preserveCoverage(std::span(packed).subspan(offset, bytes), coverage,
                       threshold);
      offset += bytes;
    }
  }
  return stop.stop_requested() ? std::vector<std::byte>{} : packed;
}
} // namespace demi::runtime::render
