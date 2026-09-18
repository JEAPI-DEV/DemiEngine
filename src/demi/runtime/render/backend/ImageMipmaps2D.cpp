#include "demi/runtime/render/backend/ImageMipmaps2D.h"
#include <algorithm>

namespace demi::runtime::render {
std::vector<std::byte> imageMipChain2D(const ImageData2D &image) {
  if (!image.width || !image.height ||
      image.rgba.size() != std::size_t(image.width) * image.height * 4)
    return {};
  std::vector<std::byte> packed = image.rgba;
  unsigned width = image.width, height = image.height;
  std::size_t offset = 0;
  while (width > 1 || height > 1) {
    const unsigned nextWidth = std::max(1U, width / 2);
    const unsigned nextHeight = std::max(1U, height / 2);
    const auto nextOffset = packed.size();
    packed.resize(nextOffset + std::size_t(nextWidth) * nextHeight * 4);
    for (unsigned y = 0; y < nextHeight; ++y)
      for (unsigned x = 0; x < nextWidth; ++x) {
        const unsigned x0 = x * width / nextWidth,
                       x1 = (x + 1) * width / nextWidth;
        const unsigned y0 = y * height / nextHeight,
                       y1 = (y + 1) * height / nextHeight;
        const unsigned count = (x1 - x0) * (y1 - y0);
        for (unsigned channel = 0; channel < 4; ++channel) {
          unsigned total = 0;
          for (unsigned sy = y0; sy < y1; ++sy)
            for (unsigned sx = x0; sx < x1; ++sx)
              total += std::to_integer<unsigned>(
                  packed[offset + (sy * width + sx) * 4 + channel]);
          packed[nextOffset + (y * nextWidth + x) * 4 + channel] =
              std::byte((total + count / 2) / count);
        }
      }
    offset = nextOffset;
    width = nextWidth;
    height = nextHeight;
  }
  return packed;
}
} // namespace demi::runtime::render
