#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/profiling/ProcessResourceSampler.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

namespace {

struct ProfileEntry {
  double totalMilliseconds = 0.0;
  double latestMilliseconds = 0.0;
  double maxMilliseconds = 0.0;
  int calls = 0;
  std::size_t bytes = 0;
  double gauge = 0.0;
  bool hasGauge = false;
  std::deque<double> samples;
};

thread_local bool scopesSuppressed = false;
thread_local bool ownsFrameProfiler = false;
bool profilerEnabled = false;
std::unordered_map<std::string, ProfileEntry> currentFrameData;
std::unordered_map<std::string, ProfileEntry> sessionData;
std::size_t sessionFrames = 0;
constexpr std::size_t MaximumSamples = 600;
ProcessResourceSampler processResources;
bool captureEnabled = false;
RuntimeProfiler::CaptureHistory frameCaptures;
std::vector<RuntimeProfiler::ScopeEvent> scopeEvents;
std::vector<RuntimeProfiler::ScopeEvent> gpuEvents;
std::size_t droppedScopes = 0;
std::size_t scopeDepth = 0;
std::uint64_t generation = 0;
std::uint64_t captureGeneration = 0;
auto frameStarted = std::chrono::steady_clock::now();
constexpr std::size_t MaximumCapturedFrames = 240;
constexpr std::size_t MaximumScopeEvents = 4096;

void clearCaptures() {
  frameCaptures.clear();
  scopeEvents.clear();
  gpuEvents.clear();
  droppedScopes = 0;
  scopeDepth = 0;
  ++generation;
  ++captureGeneration;
}

template <typename Value>
void recordResourceGauge(const std::string &name,
                         const std::optional<Value> &value) {
  if (value) {
    RuntimeProfiler::setGauge(name, static_cast<double>(*value));
  } else {
    // A failed platform read must not leave a stale value looking current.
    currentFrameData.erase(name);
    sessionData.erase(name);
  }
}

void recordProcessResources() {
  processResources.poll();
  const auto &sample = processResources.snapshot();
  recordResourceGauge("Process.cpu_percent", sample.cpuPercent);
  recordResourceGauge("Process.resident_bytes", sample.rssBytes);
  recordResourceGauge("Process.peak_resident_bytes", sample.peakRssBytes);
  if (sample.sampledAt) {
    const double age =
        std::chrono::duration<double, std::milli>(
            ProcessResourceSampler::Clock::now() - *sample.sampledAt)
            .count();
    RuntimeProfiler::setGauge("Process.sample_age_ms", std::max(0.0, age));
  }
}

void updateEntry(ProfileEntry &entry, const double milliseconds) {
  entry.totalMilliseconds += milliseconds;
  entry.latestMilliseconds = milliseconds;
  entry.maxMilliseconds = std::max(entry.maxMilliseconds, milliseconds);
  ++entry.calls;
  entry.samples.push_back(milliseconds);
  if (entry.samples.size() > MaximumSamples)
    entry.samples.pop_front();
}

double percentile(const std::vector<double> &sorted, double quantile) {
  if (sorted.empty()) {
    return 0.0;
  }
  const std::size_t index = static_cast<std::size_t>(
      std::ceil(quantile * static_cast<double>(sorted.size())) - 1.0);
  return sorted[std::min(index, sorted.size() - 1)];
}

RuntimeProfiler::Entry publicEntry(const std::string &name,
                                   const ProfileEntry &entry) {
  std::vector<double> sorted(entry.samples.begin(), entry.samples.end());
  std::ranges::sort(sorted);
  return {.name = name,
          .totalMilliseconds = entry.totalMilliseconds,
          .latestMilliseconds = entry.latestMilliseconds,
          .maxMilliseconds = entry.maxMilliseconds,
          .p95Milliseconds = percentile(sorted, 0.95),
          .calls = entry.calls,
          .bytes = entry.bytes,
          .gauge = entry.gauge,
          .hasGauge = entry.hasGauge,
          .p50Milliseconds = percentile(sorted, 0.50),
          .p99Milliseconds = percentile(sorted, 0.99),
          .sampleCount = entry.samples.size()};
}

} // namespace

void RuntimeProfiler::setCaptureEnabled(const bool enabled) {
  if (captureEnabled != enabled) {
    frameCaptures.clear();
    scopeEvents.clear();
    gpuEvents.clear();
    droppedScopes = 0;
    ++captureGeneration;
    captureEnabled = enabled;
  }
}

void RuntimeProfiler::recordGpuScope(ScopeEvent event) {
  if (enabled() && captureEnabled && gpuEvents.size() < MaximumScopeEvents) {
    gpuEvents.push_back(std::move(event));
  }
}

const RuntimeProfiler::CaptureHistory &RuntimeProfiler::captures() {
  return frameCaptures;
}

std::uint64_t RuntimeProfiler::sessionGeneration() { return generation; }

void RuntimeProfiler::setEnabled(const bool enabled) {
  ownsFrameProfiler = true;
  if (enabled != profilerEnabled)
    processResources.reset();
  profilerEnabled = enabled;
  if (!profilerEnabled) {
    clearCaptures();
    currentFrameData.clear();
    sessionData.clear();
    sessionFrames = 0;
  }
}

bool RuntimeProfiler::enabled() {
  return ownsFrameProfiler && !scopesSuppressed && profilerEnabled;
}

void RuntimeProfiler::resetSession() {
  clearCaptures();
  processResources.reset();
  currentFrameData.clear();
  sessionData.clear();
  sessionFrames = 0;
  if (profilerEnabled) {
    for (const char *scope :
         {"Frame.total", "Lua.update", "Render.total", "Physics2D.step",
          "Asset.registry_load", "Network.update"})
      sessionData.try_emplace(scope);
  }
}

void RuntimeProfiler::beginFrame() {
  if (profilerEnabled) {
    const auto now = std::chrono::steady_clock::now();
    if (captureEnabled && sessionFrames > 0) {
      frameCaptures.push_back(std::make_shared<const FrameCapture>(FrameCapture{
          .number = sessionFrames,
          .durationMilliseconds =
              std::chrono::duration<double, std::milli>(now - frameStarted)
                  .count(),
          .entries = frameEntries(),
          .scopes = std::move(scopeEvents),
          .gpuScopes = std::move(gpuEvents),
          .droppedScopes = droppedScopes}));
      if (frameCaptures.size() > MaximumCapturedFrames) {
        frameCaptures.pop_front();
      }
    }
    scopeEvents.clear();
    gpuEvents.clear();
    droppedScopes = 0;
    scopeDepth = 0;
    frameStarted = now;
    currentFrameData.clear();
    ++sessionFrames;
    recordProcessResources();
  }
}

void RuntimeProfiler::record(std::string name, const double milliseconds) {
  if (!enabled() || name.empty()) {
    return;
  }
  updateEntry(currentFrameData[name], milliseconds);
  updateEntry(sessionData[std::move(name)], milliseconds);
}

void RuntimeProfiler::addBytes(std::string name, const std::size_t bytes) {
  if (!enabled() || name.empty() || bytes == 0) {
    return;
  }
  currentFrameData[name].bytes += bytes;
  sessionData[std::move(name)].bytes += bytes;
}

void RuntimeProfiler::setGauge(std::string name, const double value) {
  if (!enabled() || name.empty())
    return;
  auto set = [value](ProfileEntry &entry) {
    entry.gauge = value;
    entry.hasGauge = true;
  };
  set(currentFrameData[name]);
  set(sessionData[std::move(name)]);
}

std::vector<RuntimeProfiler::Entry> RuntimeProfiler::sessionEntries() {
  std::vector<Entry> result;
  result.reserve(sessionData.size());
  for (const auto &[name, entry] : sessionData) {
    result.push_back(publicEntry(name, entry));
  }
  std::ranges::sort(result, [](const Entry &left, const Entry &right) {
    return left.totalMilliseconds > right.totalMilliseconds;
  });
  return result;
}

std::vector<RuntimeProfiler::Entry> RuntimeProfiler::frameEntries() {
  std::vector<Entry> result;
  result.reserve(currentFrameData.size());
  for (const auto &[name, entry] : currentFrameData)
    result.push_back(publicEntry(name, entry));
  std::ranges::sort(result, [](const Entry &left, const Entry &right) {
    return left.totalMilliseconds > right.totalMilliseconds;
  });
  return result;
}

std::size_t RuntimeProfiler::frameCount() { return sessionFrames; }

bool RuntimeProfiler::writeFrame(std::ostream &output, std::size_t frame) {
  if (frame == 0)
    output << "frame,scope,total_ms,calls,gauge\n";
  output << std::fixed << std::setprecision(6);
  for (const auto &entry : frameEntries()) {
    output << frame << ",\"";
    for (char character : entry.name) {
      if (character == '"')
        output << '"';
      output << character;
    }
    output << "\"," << entry.totalMilliseconds << ',' << entry.calls << ',';
    if (entry.hasGauge)
      output << entry.gauge;
    output << '\n';
  }
  return static_cast<bool>(output);
}

std::string RuntimeProfiler::sessionReport() {
  std::ostringstream output;
  output << "DemiEngine runtime profile\n"
         << "scope,total_ms,max_ms,calls,bytes,gauge,p50_ms,p95_ms,p99_ms,"
            "samples\n"
         << std::fixed << std::setprecision(3);
  for (const Entry &entry : sessionEntries()) {
    output << entry.name << ',' << entry.totalMilliseconds << ','
           << entry.maxMilliseconds << ',' << entry.calls << ',' << entry.bytes
           << ',';
    if (entry.hasGauge)
      output << entry.gauge;
    output << ',' << entry.p50Milliseconds << ',' << entry.p95Milliseconds
           << ',' << entry.p99Milliseconds << ',' << entry.sampleCount << '\n';
  }
  return output.str();
}

std::string RuntimeProfiler::frameSummary(const double minimumMilliseconds) {
  if (!profilerEnabled || currentFrameData.empty()) {
    return {};
  }

  std::vector<std::pair<std::string, ProfileEntry>> entries;
  entries.reserve(currentFrameData.size());
  for (const auto &[name, entry] : currentFrameData) {
    if (entry.totalMilliseconds >= minimumMilliseconds || entry.bytes > 0 ||
        entry.hasGauge) {
      entries.emplace_back(name, entry);
    }
  }
  std::ranges::sort(entries, [](const auto &left, const auto &right) {
    return left.second.totalMilliseconds > right.second.totalMilliseconds;
  });

  std::ostringstream output;
  output << std::fixed << std::setprecision(3);
  for (std::size_t index = 0; index < entries.size(); ++index) {
    if (index > 0) {
      output << ", ";
    }
    const auto &[name, entry] = entries[index];
    output << name << '=';
    if (entry.hasGauge)
      output << entry.gauge;
    else
      output << entry.totalMilliseconds << "ms";
    if (!entry.hasGauge && entry.calls > 1)
      output << '/' << entry.calls << "x max=" << entry.maxMilliseconds << "ms";
    if (entry.bytes > 0) {
      output << " bytes=" << entry.bytes;
    }
  }
  return output.str();
}

ScopedProfileSuppression::ScopedProfileSuppression()
    : previous_(scopesSuppressed) {
  scopesSuppressed = true;
}
ScopedProfileSuppression::~ScopedProfileSuppression() {
  scopesSuppressed = previous_;
}

ProfileScope::ProfileScope(std::string name) : name_(std::move(name)) {
  // A suppressed worker must not read any main-thread profiler state, even
  // when the UI toggles capture or advances frames concurrently.
  if (scopesSuppressed) {
    return;
  }
  enabled_ = RuntimeProfiler::enabled();
  if (enabled_) {
    start_ = std::chrono::steady_clock::now();
    frame_ = sessionFrames;
    depth_ = scopeDepth;
    generation_ = generation;
    captureGeneration_ = captureGeneration;
    ++scopeDepth;
  }
}

ProfileScope::~ProfileScope() {
  if (!enabled_ || !RuntimeProfiler::enabled() || generation_ != generation) {
    return;
  }
  if (frame_ == sessionFrames && scopeDepth > 0) {
    --scopeDepth;
  }
  const double milliseconds = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - start_)
                                  .count();
  if (captureEnabled && sessionFrames > 0 && frame_ == sessionFrames &&
      captureGeneration_ == captureGeneration) {
    if (scopeEvents.size() < MaximumScopeEvents) {
      scopeEvents.push_back(
          {.name = name_,
           .startMilliseconds =
               std::chrono::duration<double, std::milli>(start_ - frameStarted)
                   .count(),
           .durationMilliseconds = milliseconds,
           .depth = depth_});
    } else {
      ++droppedScopes;
    }
  }
  RuntimeProfiler::record(std::move(name_), milliseconds);
}

} // namespace demi::runtime
