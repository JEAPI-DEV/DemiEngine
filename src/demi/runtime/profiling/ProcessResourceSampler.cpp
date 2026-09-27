#include "demi/runtime/profiling/ProcessResourceSampler.h"

#include <algorithm>
#include <cmath>
#include <limits>

#if defined(__linux__) || defined(__ANDROID__)
#include <charconv>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/resource.h>
#endif

namespace demi::runtime {
namespace {

#if defined(__linux__) || defined(__ANDROID__)
std::optional<std::uint64_t> parseKilobytes(std::string_view value) {
  const auto first = value.find_first_not_of(" \t");
  if (first == std::string_view::npos)
    return std::nullopt;
  value.remove_prefix(first);
  std::uint64_t kilobytes = 0;
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), kilobytes);
  if (result.ec != std::errc{} || result.ptr == value.data() ||
      value.substr(static_cast<std::size_t>(result.ptr - value.data())) !=
          " kB" ||
      kilobytes > std::numeric_limits<std::uint64_t>::max() / 1024)
    return std::nullopt;
  return kilobytes * 1024;
}

ProcessResourceCounters readProcessCounters() {
  ProcessResourceCounters counters;
  std::ifstream status("/proc/self/status");
  for (std::string line; std::getline(status, line);) {
    if (line.starts_with("VmRSS:"))
      counters.rssBytes = parseKilobytes(std::string_view(line).substr(6));
    else if (line.starts_with("VmHWM:"))
      counters.peakRssBytes = parseKilobytes(std::string_view(line).substr(6));
  }

  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    counters.cpuSeconds =
        static_cast<double>(usage.ru_utime.tv_sec) +
        static_cast<double>(usage.ru_stime.tv_sec) +
        static_cast<double>(usage.ru_utime.tv_usec) / 1'000'000.0 +
        static_cast<double>(usage.ru_stime.tv_usec) / 1'000'000.0;
  }
  return counters;
}
#else
ProcessResourceCounters readProcessCounters() { return {}; }
#endif

} // namespace

ProcessResourceSampler::ProcessResourceSampler(
    std::chrono::milliseconds interval)
    : interval_(std::max(interval, std::chrono::milliseconds(1))) {}

bool ProcessResourceSampler::isDue(Clock::time_point now) const {
  return !sample_.sampledAt || now < *sample_.sampledAt ||
         now - *sample_.sampledAt >= interval_;
}

bool ProcessResourceSampler::poll() {
  const auto now = Clock::now();
  if (!isDue(now))
    return false;
  const auto counters = readProcessCounters();
  return observe(Clock::now(), counters);
}

bool ProcessResourceSampler::observe(Clock::time_point now,
                                     const ProcessResourceCounters &counters) {
  if (!isDue(now))
    return false;

  const auto previousTime = sample_.sampledAt;
  const auto cpuSeconds = counters.cpuSeconds &&
                                  std::isfinite(*counters.cpuSeconds) &&
                                  *counters.cpuSeconds >= 0.0
                              ? counters.cpuSeconds
                              : std::nullopt;
  sample_.cpuPercent.reset();
  if (previousTime && now > *previousTime && previousCpuSeconds_ &&
      cpuSeconds) {
    const double wallSeconds =
        std::chrono::duration<double>(now - *previousTime).count();
    sample_.cpuPercent =
        cpuPercentForInterval(*cpuSeconds - *previousCpuSeconds_, wallSeconds);
  }
  previousCpuSeconds_ = cpuSeconds;
  sample_.rssBytes = counters.rssBytes;
  sample_.peakRssBytes = counters.peakRssBytes;
  sample_.sampledAt = now;
  ++sample_.serial;
  sample_.available = cpuSeconds.has_value() || sample_.rssBytes.has_value() ||
                      sample_.peakRssBytes.has_value();
  return true;
}

void ProcessResourceSampler::reset() {
  sample_ = {};
  previousCpuSeconds_.reset();
}

std::optional<double>
ProcessResourceSampler::cpuPercentForInterval(double cpuDeltaSeconds,
                                              double wallDeltaSeconds) {
  if (!std::isfinite(cpuDeltaSeconds) || !std::isfinite(wallDeltaSeconds) ||
      cpuDeltaSeconds < 0.0 || wallDeltaSeconds <= 0.0)
    return std::nullopt;
  const double percent = 100.0 * cpuDeltaSeconds / wallDeltaSeconds;
  return std::isfinite(percent) ? std::optional<double>(percent) : std::nullopt;
}

} // namespace demi::runtime
