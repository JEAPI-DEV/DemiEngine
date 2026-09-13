#pragma once

#include <cmath>

namespace demi::runtime {

// Returns true when presenting already enforces the requested upper bound.
// A platform frame-rate request is authoritative because Android can select a
// compositor cadence that is not the display's cadence from the previous
// frame. Without that support, vsync only satisfies caps at or above the
// current refresh rate.
[[nodiscard]] inline bool
compositorSatisfiesFrameCap(const bool isVsyncEnabled, const bool isHeadless,
                            const int maximumFps, const float displayRefreshHz,
                            const bool isPlatformRateRequested) {
  if (!isVsyncEnabled || isHeadless)
    return false;
  if (maximumFps <= 0 || isPlatformRateRequested)
    return true;
  return std::isfinite(displayRefreshHz) && displayRefreshHz > 0.0F &&
         displayRefreshHz <= static_cast<float>(maximumFps) + 0.5F;
}

} // namespace demi::runtime
