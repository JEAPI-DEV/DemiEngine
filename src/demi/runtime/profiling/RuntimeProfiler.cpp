#include "demi/runtime/profiling/RuntimeProfiler.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

namespace {

struct ProfileEntry {
  double totalMilliseconds = 0.0;
  double maxMilliseconds = 0.0;
  int calls = 0;
  std::size_t bytes = 0;
};

bool profilerEnabled = false;
std::unordered_map<std::string, ProfileEntry> frameEntries;

} // namespace

void RuntimeProfiler::setEnabled(const bool enabled) {
  profilerEnabled = enabled;
  if (!profilerEnabled) {
    frameEntries.clear();
  }
}

bool RuntimeProfiler::enabled() {
  return profilerEnabled;
}

void RuntimeProfiler::beginFrame() {
  if (profilerEnabled) {
    frameEntries.clear();
  }
}

void RuntimeProfiler::record(std::string name, const double milliseconds) {
  if (!profilerEnabled || name.empty()) {
    return;
  }
  ProfileEntry& entry = frameEntries[std::move(name)];
  entry.totalMilliseconds += milliseconds;
  entry.maxMilliseconds = std::max(entry.maxMilliseconds, milliseconds);
  ++entry.calls;
}

void RuntimeProfiler::addBytes(std::string name, const std::size_t bytes) {
  if (!profilerEnabled || name.empty() || bytes == 0) {
    return;
  }
  frameEntries[std::move(name)].bytes += bytes;
}

std::string RuntimeProfiler::frameSummary(const double minimumMilliseconds) {
  if (!profilerEnabled || frameEntries.empty()) {
    return {};
  }

  std::vector<std::pair<std::string, ProfileEntry>> entries;
  entries.reserve(frameEntries.size());
  for (const auto& [name, entry] : frameEntries) {
    if (entry.totalMilliseconds >= minimumMilliseconds || entry.bytes > 0) {
      entries.emplace_back(name, entry);
    }
  }
  std::ranges::sort(entries, [](const auto& left, const auto& right) {
    return left.second.totalMilliseconds > right.second.totalMilliseconds;
  });

  std::ostringstream output;
  output << std::fixed << std::setprecision(3);
  for (std::size_t index = 0; index < entries.size(); ++index) {
    if (index > 0) {
      output << ", ";
    }
    const auto& [name, entry] = entries[index];
    output << name << '=' << entry.totalMilliseconds << "ms";
    if (entry.calls > 1) {
      output << '/' << entry.calls << "x max=" << entry.maxMilliseconds << "ms";
    }
    if (entry.bytes > 0) {
      output << " bytes=" << entry.bytes;
    }
  }
  return output.str();
}

ProfileScope::ProfileScope(std::string name) : name_(std::move(name)), start_(std::chrono::steady_clock::now()), enabled_(RuntimeProfiler::enabled()) {}

ProfileScope::~ProfileScope() {
  if (!enabled_) {
    return;
  }
  const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
  RuntimeProfiler::record(std::move(name_), milliseconds);
}

} // namespace demi::runtime
