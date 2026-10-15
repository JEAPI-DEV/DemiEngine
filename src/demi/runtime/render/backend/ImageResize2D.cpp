#include "demi/runtime/render/backend/ImageResize2D.h"

#include "demi/runtime/render/backend/ImageColorSpace.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace demi::runtime::render {
namespace {

std::byte channelByte(double value) {
  return static_cast<std::byte>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
}

} // namespace

// Area filtering downsamples without losing narrow features. A one-source-pixel
// footprint also supplies linear interpolation when increasing resolution.
ImageData2D resizeImage2D(const ImageData2D &source, std::uint16_t width,
                          std::uint16_t height, ImageMipSemantic semantic,
                          std::stop_token stop) {
  const auto expectedBytes = std::size_t(source.width) * source.height * 4;
  if (width == 0 || height == 0 || source.width == 0 || source.height == 0 ||
      source.rgba.size() != expectedBytes || stop.stop_requested()) {
    return {};
  }
  if (source.width == width && source.height == height) {
    return source;
  }
  ImageData2D result{.width = width, .height = height};
  result.rgba.resize(std::size_t(width) * height * 4);
  const double scaleX = double(source.width) / width;
  const double scaleY = double(source.height) / height;
  const double footprintX = std::max(1.0, scaleX);
  const double footprintY = std::max(1.0, scaleY);
  for (unsigned y = 0; y < height; ++y) {
    if (stop.stop_requested()) {
      return {};
    }
    const double centerY = (y + 0.5) * scaleY;
    const double lowerY = centerY - footprintY * 0.5;
    const double upperY = centerY + footprintY * 0.5;
    for (unsigned x = 0; x < width; ++x) {
      const double centerX = (x + 0.5) * scaleX;
      const double lowerX = centerX - footprintX * 0.5;
      const double upperX = centerX + footprintX * 0.5;
      std::array<double, 4> sum{};
      double total = 0;
      const int firstY = static_cast<int>(std::floor(lowerY));
      const int lastY = static_cast<int>(std::ceil(upperY));
      const int firstX = static_cast<int>(std::floor(lowerX));
      const int lastX = static_cast<int>(std::ceil(upperX));
      for (int sourceY = firstY; sourceY < lastY; ++sourceY) {
        if ((sourceY & 255) == 0 && stop.stop_requested()) {
          return {};
        }
        const double weightY = std::min(upperY, double(sourceY + 1)) -
                               std::max(lowerY, double(sourceY));
        for (int sourceX = firstX; sourceX < lastX; ++sourceX) {
          const double weightX = std::min(upperX, double(sourceX + 1)) -
                                 std::max(lowerX, double(sourceX));
          const double weight = weightX * weightY;
          const auto sx = std::clamp(sourceX, 0, int(source.width) - 1);
          const auto sy = std::clamp(sourceY, 0, int(source.height) - 1);
          const auto offset = (std::size_t(sy) * source.width + sx) * 4;
          for (std::size_t channel = 0; channel < 4; ++channel) {
            double value =
                std::to_integer<unsigned>(source.rgba[offset + channel]) /
                255.0;
            if (channel < 3 && semantic == ImageMipSemantic::Color) {
              value = decodeSrgbChannel(value);
            } else if (channel < 3 && semantic == ImageMipSemantic::Normal) {
              value = value * 2 - 1;
            }
            sum[channel] += value * weight;
          }
          total += weight;
        }
      }
      for (auto &channel : sum) {
        channel /= total;
      }
      if (semantic == ImageMipSemantic::Normal) {
        const double length = std::hypot(sum[0], sum[1], sum[2]);
        if (length > 1e-12) {
          for (std::size_t channel = 0; channel < 3; ++channel) {
            sum[channel] /= length;
          }
        } else {
          sum[0] = 0;
          sum[1] = 0;
          sum[2] = 1;
        }
      }
      const auto offset = (std::size_t(y) * width + x) * 4;
      for (std::size_t channel = 0; channel < 4; ++channel) {
        double value = sum[channel];
        if (channel < 3 && semantic == ImageMipSemantic::Color) {
          value = encodeSrgbChannel(value);
        } else if (channel < 3 && semantic == ImageMipSemantic::Normal) {
          value = value * 0.5 + 0.5;
        }
        result.rgba[offset + channel] = channelByte(value);
      }
    }
  }
  return result;
}

} // namespace demi::runtime::render
