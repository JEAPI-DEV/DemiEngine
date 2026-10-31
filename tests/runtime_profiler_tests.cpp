#include "demi/runtime/diagnostics/RuntimeLog.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"

#include <algorithm>
#include <iostream>
#include <thread>

int main() {
  using demi::runtime::RuntimeProfiler;
  RuntimeProfiler::setEnabled(true);
  RuntimeProfiler::resetSession();
  RuntimeProfiler::beginFrame();
  std::thread worker([] {
    {
      demi::runtime::ProfileScope scope("Background.unowned_scope");
      RuntimeProfiler::setGauge("Background.unowned_gauge", 1);
      RuntimeProfiler::record("Background.unowned_record", 1);
    }
    demi::runtime::ScopedProfileSuppression suppression;
    for (int index = 0; index < 1000; ++index) {
      demi::runtime::ProfileScope scope("Background.must_not_enter_frame");
      RuntimeProfiler::setGauge("Background.gauge", 1);
      RuntimeProfiler::addBytes("Background.bytes", 10);
      RuntimeProfiler::record("Background.direct", 2);
      RuntimeProfiler::recordGpuScope({.name = "Background.gpu"});
      demi::runtime::ScopedProfileSuppression nested;
      demi::runtime::ProfileScope inner("Background.nested");
    }
  });
  for (int index = 0; index < 100; ++index) {
    RuntimeProfiler::beginFrame();
  }
  worker.join();
  if (std::ranges::any_of(RuntimeProfiler::sessionEntries(),
                          [](const auto &entry) {
                            return entry.name.starts_with("Background.");
                          })) {
    std::cerr << "Suppressed worker scopes entered main-thread profiler\n";
    return 1;
  }
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
      RuntimeProfiler::frameCount() != 2 ||
      std::ranges::count_if(current,
                            [](const auto &entry) {
                              return !entry.name.starts_with("Process.");
                            }) != 1 ||
      !std::ranges::any_of(
          current,
          [](const auto &entry) { return entry.name == "Lua.update"; }) ||
      report.find("Lua.update,3.250,2.000,2") == std::string::npos ||
      report.find("Physics2D.step") == std::string::npos ||
      report.find("Network.update") == std::string::npos ||
      report.find("Asset.upload") == std::string::npos ||
      report.find("Renderer3D.stats.batches,0.000,0.000,0,0,12.000") ==
          std::string::npos) {
    std::cerr << "session profiler report lost cross-frame category data\n";
    return 1;
  }
  RuntimeProfiler::resetSession();
  for (int sample = 1; sample <= 700; ++sample)
    RuntimeProfiler::record("Percentile.test", static_cast<double>(sample));
  const auto quantiles = RuntimeProfiler::sessionEntries();
  const auto measured = std::ranges::find(quantiles, "Percentile.test",
                                          &RuntimeProfiler::Entry::name);
  if (measured == quantiles.end() || measured->sampleCount != 600 ||
      measured->p50Milliseconds != 400 || measured->p95Milliseconds != 670 ||
      measured->p99Milliseconds != 694 || measured->calls != 700 ||
      measured->maxMilliseconds != 700 ||
      RuntimeProfiler::sessionReport().find("p50_ms,p95_ms,p99_ms,samples") ==
          std::string::npos) {
    std::cerr << "profiler percentile window or CSV columns incorrect\n";
    return 1;
  }
  RuntimeProfiler::resetSession();
  RuntimeProfiler::setCaptureEnabled(true);
  RuntimeProfiler::beginFrame();
  {
    demi::runtime::ProfileScope outer("Trace.outer");
    {
      demi::runtime::ProfileScope inner("Trace.inner");
    }
  }
  RuntimeProfiler::setGauge("Trace.draws", 17);
  RuntimeProfiler::recordGpuScope(
      {.name = "shadow", .startMilliseconds = 2, .durationMilliseconds = 3});
  RuntimeProfiler::beginFrame();
  const auto &captured = RuntimeProfiler::captures();
  if (captured.size() != 1 || captured.front()->number != 1 ||
      captured.front()->scopes.size() != 2 ||
      captured.front()->scopes[0].depth != 1 ||
      captured.front()->scopes[1].depth != 0 ||
      captured.front()->scopes[0].startMilliseconds <
          captured.front()->scopes[1].startMilliseconds ||
      captured.front()->gpuScopes.size() != 1 ||
      captured.front()->gpuScopes[0].startMilliseconds != 2 ||
      !std::ranges::any_of(
          captured.front()->entries,
          [](const auto &entry) {
            return entry.name == "Trace.draws" && entry.gauge == 17;
          })) {
    std::cerr << "completed frame lost nested scopes, GPU timing or counters\n";
    return 1;
  }
  for (int index = 0; index < 4100; ++index) {
    demi::runtime::ProfileScope event("Trace.bounded");
  }
  RuntimeProfiler::beginFrame();
  if (captured.back()->scopes.size() != 4096 ||
      captured.back()->droppedScopes != 4) {
    std::cerr << "scope capture was not bounded\n";
    return 1;
  }
  for (int index = 0; index < 250; ++index) {
    RuntimeProfiler::beginFrame();
  }
  if (captured.size() != 240 || captured.back()->number != 252) {
    std::cerr << "frame history was not bounded and ordered\n";
    return 1;
  }
  {
    demi::runtime::ProfileScope aggregate("Trace.toggle");
    RuntimeProfiler::setCaptureEnabled(false);
    RuntimeProfiler::setCaptureEnabled(true);
  }
  if (!std::ranges::any_of(
          RuntimeProfiler::frameEntries(), [](const auto &entry) {
            return entry.name == "Trace.toggle" && entry.calls == 1;
          })) {
    std::cerr << "trace toggling discarded aggregate scope timing\n";
    return 1;
  }
  const auto previousGeneration = RuntimeProfiler::sessionGeneration();
  {
    demi::runtime::ProfileScope stale("Trace.stale");
    RuntimeProfiler::resetSession();
    RuntimeProfiler::beginFrame();
  }
  RuntimeProfiler::beginFrame();
  if (RuntimeProfiler::sessionGeneration() == previousGeneration ||
      captured.size() != 1 || !captured.front()->scopes.empty()) {
    std::cerr << "session reset retained stale trace events\n";
    return 1;
  }
  RuntimeProfiler::setCaptureEnabled(false);
  RuntimeProfiler::beginFrame();
  if (!captured.empty()) {
    std::cerr << "disabled capture retained history\n";
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
