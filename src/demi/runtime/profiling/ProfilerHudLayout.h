#pragma once

namespace demi::runtime {

struct ProfilerHudLayout {
  int margin = 8;
  int padding = 6;
  int fontSize = 16;
  int width = 1;
  int height = 1;
};

[[nodiscard]] ProfilerHudLayout profilerHudLayout(int viewportWidth,
                                                  int viewportHeight);

} // namespace demi::runtime
