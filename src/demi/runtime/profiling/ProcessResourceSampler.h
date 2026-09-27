#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace demi::runtime {

struct ProcessResourceCounters {
  std::optional<double> cpuSeconds;
  std::optional<std::uint64_t> rssBytes;
  std::optional<std::uint64_t> peakRssBytes;
};

struct ProcessResourceSample {
  // 100% is one logical core; a multithreaded process can exceed 100%.
  std::optional<double> cpuPercent;
  std::optional<std::uint64_t> rssBytes;
  // Kernel high-water RSS since process start, including across sampler resets.
  std::optional<std::uint64_t> peakRssBytes;
  std::optional<std::chrono::steady_clock::time_point> sampledAt;
  std::uint64_t serial = 0;
  bool available = false;
};

class ProcessResourceSampler {
public:
  using Clock = std::chrono::steady_clock;

  explicit ProcessResourceSampler(
      std::chrono::milliseconds interval = std::chrono::milliseconds(250));

  // Returns true only when a new reading was accepted; an unsupported platform
  // still advances the serial so consumers can distinguish stale from absent.
  bool poll();
  bool observe(Clock::time_point now, const ProcessResourceCounters &counters);
  [[nodiscard]] const ProcessResourceSample &snapshot() const {
    return sample_;
  }
  void reset();

  [[nodiscard]] static std::optional<double>
  cpuPercentForInterval(double cpuDeltaSeconds, double wallDeltaSeconds);

private:
  [[nodiscard]] bool isDue(Clock::time_point now) const;

  std::chrono::milliseconds interval_;
  ProcessResourceSample sample_;
  std::optional<double> previousCpuSeconds_;
};

} // namespace demi::runtime
