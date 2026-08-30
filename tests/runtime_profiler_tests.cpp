#include "demi/runtime/diagnostics/RuntimeLog.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"

#include <algorithm>
#include <iostream>

int main() {
  using demi::runtime::RuntimeProfiler;
  RuntimeProfiler::setEnabled(true);
  RuntimeProfiler::resetSession();
  RuntimeProfiler::beginFrame();
  RuntimeProfiler::record("Lua.update", 1.25);
  RuntimeProfiler::record("Physics2D.step", 0.5);
  RuntimeProfiler::record("Network.update", 0.1);
  RuntimeProfiler::addBytes("Asset.upload", 4096);
  RuntimeProfiler::setGauge("Renderer3D.stats.batches", 12.0);
  RuntimeProfiler::beginFrame();
  RuntimeProfiler::record("Lua.update", 2.0);

  const auto entries = RuntimeProfiler::sessionEntries();
  const auto current = RuntimeProfiler::frameEntries();
  const auto lua =
      std::ranges::find(entries, "Lua.update", &RuntimeProfiler::Entry::name);
  const std::string report = RuntimeProfiler::sessionReport();
  if (entries.empty() || lua == entries.end() ||
      lua->latestMilliseconds != 2.0 || lua->p95Milliseconds != 2.0 ||
      RuntimeProfiler::frameCount() != 2 || current.size() != 1 ||
      current.front().name != "Lua.update" ||
      report.find("Lua.update,3.250,2.000,2") == std::string::npos ||
      report.find("Physics2D.step") == std::string::npos ||
      report.find("Network.update") == std::string::npos ||
      report.find("Asset.upload") == std::string::npos ||
      report.find("Renderer3D.stats.batches,0.000,0.000,0,0,12.000") ==
          std::string::npos) {
    std::cerr << "session profiler report lost cross-frame category data\n";
    return 1;
  }
  RuntimeProfiler::setEnabled(false);

  demi::runtime::RuntimeLogBuffer logs(2);
  logs.append({.channel = "test", .message = "one"});
  logs.append({.channel = "test", .message = "two"});
  logs.append({.channel = "test", .message = "three"});
  const auto retained = logs.entries();
  if (retained.size() != 2 || retained.front().message != "two" ||
      retained.back().message != "three" ||
      retained.front().sequence >= retained.back().sequence) {
    std::cerr << "runtime log buffer was not bounded and ordered\n";
    return 1;
  }
  return 0;
}
