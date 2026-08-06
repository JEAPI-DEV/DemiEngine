#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace demi::runtime::render {

struct TextureAnimation2D {
  std::size_t frameCount = 0;
  std::vector<float> frameDurations;
};

[[nodiscard]] inline std::size_t
textureAnimationFrameAt(const TextureAnimation2D &animation,
                        const float animationTime) {
  if (animation.frameCount == 0)
    return 0;
  if (animation.frameDurations.empty())
    return static_cast<std::size_t>(std::max(animationTime, 0.0F) * 10.0F) %
           animation.frameCount;

  float cycle = 0.0F;
  for (const float duration : animation.frameDurations)
    cycle += std::max(duration, 0.001F);
  float time =
      std::fmod(std::max(animationTime, 0.0F), std::max(cycle, 0.001F));
  for (std::size_t frame = 0; frame < animation.frameDurations.size();
       ++frame) {
    time -= std::max(animation.frameDurations[frame], 0.001F);
    if (time <= 0.0F)
      return frame % animation.frameCount;
  }
  return 0;
}

} // namespace demi::runtime::render
