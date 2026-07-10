#pragma once

#include <vector>

namespace demi::runtime {

struct ImageAnimationTextureData {
  int frameCount = 0;
};

struct GifAnimationTextureData {
  std::vector<float> frameDurations;
};

} // namespace demi::runtime
