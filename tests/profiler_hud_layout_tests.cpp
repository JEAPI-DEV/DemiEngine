#include "demi/runtime/profiling/ProfilerHudLayout.h"

#include <iostream>

int main() {
  using demi::runtime::profilerHudLayout;
  const auto hd = profilerHudLayout(960, 540);
  const auto large = profilerHudLayout(2048, 1152);
  if (hd.fontSize < 16 || large.fontSize < 24 ||
      large.fontSize <= hd.fontSize || large.width <= hd.width ||
      large.height <= hd.height) {
    std::cerr << "profiler HUD does not scale readably with the viewport\n";
    return 1;
  }
  return 0;
}
