#include "demi/runtime/profiling/RuntimeProfiler.h"

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

bool profilerEnabled = false;
std::unordered_map<std::string, ProfileEntry> currentFrameData;
std::unordered_map<std::string, ProfileEntry> sessionData;
std::size_t sessionFrames = 0;
constexpr std::size_t MaximumSamples = 600;

void updateEntry(ProfileEntry &entry, const double milliseconds) {
  entry.totalMilliseconds += milliseconds;
  entry.latestMilliseconds = milliseconds;
  entry.maxMilliseconds = std::max(entry.maxMilliseconds, milliseconds);
  ++entry.calls;
  entry.samples.push_back(milliseconds);
  if (entry.samples.size() > MaximumSamples)
    entry.samples.pop_front();
}

double percentile95(const ProfileEntry &entry) {
  if (entry.samples.empty())
    return 0.0;
  std::vector<double> sorted(entry.samples.begin(), entry.samples.end());
  std::ranges::sort(sorted);
  const std::size_t index = static_cast<std::size_t>(
      std::ceil(0.95 * static_cast<double>(sorted.size())) - 1.0);
  return sorted[std::min(index, sorted.size() - 1)];
}

RuntimeProfiler::Entry publicEntry(const std::string &name,
                                   const ProfileEntry &entry) {
  return {.name = name,
          .totalMilliseconds = entry.totalMilliseconds,
          .latestMilliseconds = entry.latestMilliseconds,
          .maxMilliseconds = entry.maxMilliseconds,
          .p95Milliseconds = percentile95(entry),
          .calls = entry.calls,
          .bytes = entry.bytes,
          .gauge = entry.gauge,
          .hasGauge = entry.hasGauge};
}

} // namespace

void RuntimeProfiler::setEnabled(const bool enabled) {
  profilerEnabled = enabled;
  if (!profilerEnabled) {
    currentFrameData.clear();
    sessionData.clear();
    sessionFrames = 0;
  }
}

bool RuntimeProfiler::enabled() { return profilerEnabled; }

void RuntimeProfiler::resetSession() {
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
    currentFrameData.clear();
    ++sessionFrames;
  }
}

void RuntimeProfiler::record(std::string name, const double milliseconds) {
  if (!profilerEnabled || name.empty()) {
    return;
  }
  updateEntry(currentFrameData[name], milliseconds);
  updateEntry(sessionData[std::move(name)], milliseconds);
}

void RuntimeProfiler::addBytes(std::string name, const std::size_t bytes) {
  if (!profilerEnabled || name.empty() || bytes == 0) {
    return;
  }
  currentFrameData[name].bytes += bytes;
  sessionData[std::move(name)].bytes += bytes;
}

void RuntimeProfiler::setGauge(std::string name, const double value) {
  if (!profilerEnabled || name.empty())
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

std::string RuntimeProfiler::sessionReport() {
  std::ostringstream output;
  output << "DemiEngine runtime profile\n"
         << "scope,total_ms,max_ms,calls,bytes,gauge\n"
         << std::fixed << std::setprecision(3);
  for (const Entry &entry : sessionEntries()) {
    output << entry.name << ',' << entry.totalMilliseconds << ','
           << entry.maxMilliseconds << ',' << entry.calls << ',' << entry.bytes
           << ',';
    if (entry.hasGauge)
      output << entry.gauge;
    output << '\n';
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

ProfileScope::ProfileScope(std::string name)
    : name_(std::move(name)), start_(std::chrono::steady_clock::now()),
      enabled_(RuntimeProfiler::enabled()) {}

ProfileScope::~ProfileScope() {
  if (!enabled_) {
    return;
  }
  const double milliseconds = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - start_)
                                  .count();
  RuntimeProfiler::record(std::move(name_), milliseconds);
}

} // namespace demi::runtime
