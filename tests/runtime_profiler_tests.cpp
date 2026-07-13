#include "demi/runtime/profiling/RuntimeProfiler.h"

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
  RuntimeProfiler::beginFrame();
  RuntimeProfiler::record("Lua.update", 2.0);

  const auto entries = RuntimeProfiler::sessionEntries();
  const std::string report = RuntimeProfiler::sessionReport();
  if (entries.empty() ||
      report.find("Lua.update,3.250,2.000,2") == std::string::npos ||
      report.find("Physics2D.step") == std::string::npos ||
      report.find("Network.update") == std::string::npos ||
      report.find("Asset.upload") == std::string::npos) {
    std::cerr << "session profiler report lost cross-frame category data\n";
    return 1;
  }
  RuntimeProfiler::setEnabled(false);
  return 0;
}
