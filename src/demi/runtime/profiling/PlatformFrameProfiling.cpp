#include "demi/runtime/profiling/PlatformFrameProfiling.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include <algorithm>

namespace demi::runtime {
void recordPlatformFrameTiming(const platform::PlatformFrameState &state,
                               bool firstFrame) {
  if (!RuntimeProfiler::enabled())
    return;
  if (!firstFrame)
    RuntimeProfiler::record("Frame.interval", state.wallDeltaSeconds * 1000.0);
  RuntimeProfiler::setGauge("Frame.delta_override",
                            state.deltaOverridden ? 1 : 0);
  RuntimeProfiler::setGauge(
      "Simulation.clamped_wall_ms",
      state.deltaOverridden
          ? 0
          : std::max(0.0, state.wallDeltaSeconds -
                              static_cast<double>(state.deltaSeconds)) *
                1000.0);
  RuntimeProfiler::setGauge("Window.focused", state.focused ? 1 : 0);
  RuntimeProfiler::setGauge("Window.minimized", state.minimized ? 1 : 0);
  RuntimeProfiler::setGauge("Window.refresh_hz", state.displayRefreshHz);
}
} // namespace demi::runtime
