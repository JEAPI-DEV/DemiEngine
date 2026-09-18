#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace demi::runtime::render {

struct VisualAnimationSample3D {
  double clock = 0;
  float time = 0;
  float rate = 0;
};

// Only called for a valid cached pose with unchanged structural state. Clip,
// rig, source and procedural changes must bypass this visual-only budget.
inline bool visualAnimationSampleDue(const VisualAnimationSample3D &sample,
                                     double clock, float time, float rate,
                                     std::string_view entityId) {
  if (rate <= 0 || rate != sample.rate || clock <= sample.clock ||
      time < sample.time)
    return true;
  std::uint32_t hash = 2166136261U;
  for (unsigned char c : entityId)
    hash = (hash ^ c) * 16777619U;
  const double phase = double(hash & 65535U) / 65536.0;
  return std::floor(clock * rate + phase) !=
         std::floor(sample.clock * rate + phase);
}

} // namespace demi::runtime::render
