#pragma once
#include <cstdint>
#include <optional>

namespace demi::runtime::render {
inline std::optional<double> ticksToMilliseconds(std::int64_t ticks,
                                                 std::int64_t frequency) {
  if (ticks < 0 || frequency <= 0)
    return std::nullopt;
  return static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency);
}
inline std::optional<double> intervalMilliseconds(std::int64_t begin,
                                                  std::int64_t end,
                                                  std::int64_t frequency) {
  if (begin < 0 || end < begin)
    return std::nullopt;
  return ticksToMilliseconds(end - begin, frequency);
}

// Optional samples are absent when unavailable or already reported. Backend
// CPU and GPU samples can describe older frames and must not be summed with
// current application-frame timings as if they formed one synchronous frame.
struct GraphicsFrameTimings {
  double advanceMilliseconds = 0;
  std::optional<double> renderThreadMilliseconds;
  std::optional<double> waitRenderMilliseconds;
  std::optional<double> waitSubmitMilliseconds;
  std::optional<double> gpuMilliseconds;
  std::uint32_t submittedFrame = 0;
  std::uint32_t gpuFrame = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool gpuTimerAvailable = false;
  std::uint16_t vendorId = 0;
  std::uint16_t deviceId = 0;
};

class GpuFrameSampleFilter {
public:
  void reset() { lastFrame_.reset(); }
  std::optional<double> consume(std::uint32_t frame, std::int64_t begin,
                                std::int64_t end, std::int64_t frequency) {
    const auto duration = intervalMilliseconds(begin, end, frequency);
    if (!duration || *duration <= 0)
      return std::nullopt;
    if (lastFrame_) {
      const std::uint32_t distance = frame - *lastFrame_;
      if (distance == 0 || distance > UINT32_MAX / 2)
        return std::nullopt;
    }
    lastFrame_ = frame;
    return duration;
  }

private:
  std::optional<std::uint32_t> lastFrame_;
};
} // namespace demi::runtime::render
