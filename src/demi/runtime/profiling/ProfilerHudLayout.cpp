#include "demi/runtime/profiling/ProfilerHudLayout.h"

#include <algorithm>

namespace demi::runtime {

ProfilerHudLayout profilerHudLayout(const int viewportWidth,
                                    const int viewportHeight) {
  const int safeWidth = std::max(viewportWidth, 1);
  const int safeHeight = std::max(viewportHeight, 1);
  const int fontSize = std::clamp(safeHeight / 45, 16, 28);
  const int margin = std::max(fontSize / 2, 8);
  const int padding = std::max(fontSize / 3, 6);
  return {.margin = margin,
          .padding = padding,
          .fontSize = fontSize,
          .width = std::max(safeWidth - margin * 2, 1),
          .height = fontSize + padding * 2};
}

} // namespace demi::runtime
