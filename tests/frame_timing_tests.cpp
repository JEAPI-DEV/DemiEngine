#include "cli/CliArguments.h"
#include "demi/runtime/profiling/PlatformFrameProfiling.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/backend/GraphicsFrameTimings.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace demi::runtime;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

int main() {
  try {
    using namespace render;
    require(intervalMilliseconds(100, 200, 1000) == 100.0, "timer conversion");
    require(!intervalMilliseconds(200, 100, 1000) &&
                !intervalMilliseconds(-1, 2, 1000) &&
                !intervalMilliseconds(0, 2, 0) &&
                !ticksToMilliseconds(-1, 1000),
            "invalid timestamps");
    GpuFrameSampleFilter gpu;
    require(gpu.consume(0, 10, 20, 1000) == 10.0, "GPU frame zero");
    require(!gpu.consume(0, 10, 20, 1000), "duplicate GPU frame");
    require(!gpu.consume(1, 10, 20, 0), "invalid GPU frequency");
    require(gpu.consume(1, 10, 20, 1000).has_value(),
            "invalid sample consumed frame ID");
    require(!gpu.consume(0, 10, 20, 1000), "out-of-order GPU frame");
    gpu.reset();
    require(gpu.consume(UINT32_MAX, 10, 20, 1000).has_value() &&
                gpu.consume(0, 10, 20, 1000).has_value(),
            "GPU frame wrap");
    require(demi::cli::parseWindowSize("1920x1080") == std::pair{1920, 1080},
            "window dimensions");
    for (const auto *text : {"", "1920", "x1080", "1920x", "0x1080", "-1x5",
                             "1920x1080junk", "65536x1", "1x1x1"})
      require(!demi::cli::parseWindowSize(text), "invalid window dimensions");
    RuntimeProfiler::setEnabled(true);
    RuntimeProfiler::resetSession();
    RuntimeProfiler::beginFrame();
    platform::PlatformFrameState state;
    state.wallDeltaSeconds = .2;
    state.deltaSeconds = .1F;
    state.focused = true;
    recordPlatformFrameTiming(state, false);
    const auto entries = RuntimeProfiler::frameEntries();
    auto interval = std::ranges::find(entries, "Frame.interval",
                                      &RuntimeProfiler::Entry::name);
    auto clamped = std::ranges::find(entries, "Simulation.clamped_wall_ms",
                                     &RuntimeProfiler::Entry::name);
    require(interval != entries.end() && interval->totalMilliseconds == 200,
            "raw frame interval");
    require(clamped != entries.end() && std::abs(clamped->gauge - 100) < .001,
            "clamped delta evidence");
    RuntimeProfiler::record("quote\"scope", 2.0);
    std::ostringstream csv;
    require(RuntimeProfiler::writeFrame(csv, 0), "trace output");
    require(csv.str().starts_with("frame,scope,total_ms,calls,gauge\n") &&
                csv.str().find("\"quote\"\"scope\"") != std::string::npos,
            "CSV escaping");
    std::ostringstream failed;
    failed.setstate(std::ios::badbit);
    require(!RuntimeProfiler::writeFrame(failed, 0),
            "trace write failure ignored");
    RuntimeProfiler::beginFrame();
    state.deltaOverridden = true;
    recordPlatformFrameTiming(state, true);
    const auto first = RuntimeProfiler::frameEntries();
    require(std::ranges::find(first, "Frame.interval",
                              &RuntimeProfiler::Entry::name) == first.end(),
            "startup interval counted");
    RuntimeProfiler::setEnabled(false);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
