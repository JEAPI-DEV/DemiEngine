#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace demi::runtime {

class RuntimeProfiler {
public:
  struct Entry {
    std::string name;
    double totalMilliseconds = 0.0;
    double latestMilliseconds = 0.0;
    double maxMilliseconds = 0.0;
    double p95Milliseconds = 0.0;
    int calls = 0;
    std::size_t bytes = 0;
    double gauge = 0.0;
    bool hasGauge = false;
    double p50Milliseconds = 0.0;
    double p99Milliseconds = 0.0;
    std::size_t sampleCount = 0;
  };

  struct ScopeEvent {
    std::string name;
    double startMilliseconds = 0.0;
    double durationMilliseconds = 0.0;
    std::size_t depth = 0;
  };

  struct FrameCapture {
    std::size_t number = 0;
    double durationMilliseconds = 0.0;
    std::vector<Entry> entries;
    std::vector<ScopeEvent> scopes;
    std::vector<ScopeEvent> gpuScopes;
    std::size_t droppedScopes = 0;
  };

  // Opt-in bounded capture for tooling; ordinary profiling keeps no traces.
  using CaptureHistory = std::deque<std::shared_ptr<const FrameCapture>>;

  static void setCaptureEnabled(bool enabled);
  static void recordGpuScope(ScopeEvent event);
  [[nodiscard]] static const CaptureHistory &captures();
  [[nodiscard]] static std::uint64_t sessionGeneration();
  // Control/capture methods belong to the frame thread. Scopes and metric
  // writes from other threads are ignored; background jobs own their telemetry.
  static void setEnabled(bool enabled);
  [[nodiscard]] static bool enabled();
  static void resetSession();
  static void beginFrame();
  static void record(std::string name, double milliseconds);
  static void addBytes(std::string name, std::size_t bytes);
  static void setGauge(std::string name, double value);
  [[nodiscard]] static std::string
  frameSummary(double minimumMilliseconds = 0.01);
  [[nodiscard]] static std::vector<Entry> sessionEntries();
  [[nodiscard]] static std::vector<Entry> frameEntries();
  [[nodiscard]] static std::size_t frameCount();
  [[nodiscard]] static std::string sessionReport();
  [[nodiscard]] static bool writeFrame(std::ostream &output, std::size_t frame);
};

// Background preparation may reuse code with main-thread profiling scopes.
// Suppress scopes and counter writes locally; never toggle the process-wide
// profiler from a worker. Worker timings belong to the owning job's telemetry.
class ScopedProfileSuppression {
public:
  ScopedProfileSuppression();
  ~ScopedProfileSuppression();
  ScopedProfileSuppression(const ScopedProfileSuppression &) = delete;
  ScopedProfileSuppression &
  operator=(const ScopedProfileSuppression &) = delete;

private:
  bool previous_ = false;
};

class ProfileScope {
public:
  explicit ProfileScope(std::string name);
  ~ProfileScope();

  ProfileScope(const ProfileScope &) = delete;
  ProfileScope &operator=(const ProfileScope &) = delete;

private:
  std::string name_;
  std::chrono::steady_clock::time_point start_;
  bool enabled_ = false;
  std::size_t frame_ = 0;
  std::size_t depth_ = 0;
  std::uint64_t generation_ = 0;
  std::uint64_t captureGeneration_ = 0;
};

} // namespace demi::runtime
