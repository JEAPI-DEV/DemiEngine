#include "demi/runtime/profiling/ProcessResourceSampler.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

namespace {

using demi::runtime::ProcessResourceCounters;
using demi::runtime::ProcessResourceSampler;
using namespace std::chrono_literals;

void calculationTest() {
  const auto oneCore = ProcessResourceSampler::cpuPercentForInterval(0.5, 0.5);
  const auto multipleCores =
      ProcessResourceSampler::cpuPercentForInterval(0.75, 0.25);
  assert(oneCore && *oneCore == 100.0);
  assert(multipleCores && *multipleCores == 300.0);
  assert(!ProcessResourceSampler::cpuPercentForInterval(-0.1, 1.0));
  assert(!ProcessResourceSampler::cpuPercentForInterval(1.0, 0.0));
  assert(!ProcessResourceSampler::cpuPercentForInterval(
      std::numeric_limits<double>::infinity(), 1.0));
}

void lifecycleTest() {
  ProcessResourceSampler sampler;
  const auto start = ProcessResourceSampler::Clock::time_point{};
  assert(!sampler.snapshot().available && sampler.snapshot().serial == 0);
  assert(!sampler.snapshot().sampledAt && !sampler.snapshot().cpuPercent);

  assert(sampler.observe(start, ProcessResourceCounters{2.0, 4096, 8192}));
  assert(sampler.snapshot().serial == 1 && sampler.snapshot().available);
  assert(sampler.snapshot().sampledAt == start);
  assert(!sampler.snapshot().cpuPercent);
  assert(sampler.snapshot().rssBytes == 4096);
  assert(sampler.snapshot().peakRssBytes == 8192);

  assert(!sampler.observe(start + 249ms,
                          ProcessResourceCounters{9.0, 8192, 16384}));
  assert(sampler.snapshot().serial == 1 && sampler.snapshot().rssBytes == 4096);
  assert(sampler.observe(start + 250ms,
                         ProcessResourceCounters{2.5, 8192, 16384}));
  assert(sampler.snapshot().serial == 2);
  assert(sampler.snapshot().cpuPercent == 200.0);
  assert(sampler.snapshot().peakRssBytes == 16384);

  assert(sampler.observe(start + 500ms, {}));
  assert(sampler.snapshot().serial == 3 && !sampler.snapshot().available);
  assert(!sampler.snapshot().cpuPercent && !sampler.snapshot().rssBytes &&
         !sampler.snapshot().peakRssBytes);
  assert(sampler.observe(start + 750ms,
                         ProcessResourceCounters{3.0, 2048, 16384}));
  assert(
      !sampler.snapshot().cpuPercent); // Missing CPU reading breaks baseline.
  assert(sampler.observe(start + 1000ms,
                         ProcessResourceCounters{2.0, 2048, 16384}));
  assert(!sampler.snapshot().cpuPercent); // Counter reset is a new baseline.

  sampler.reset();
  assert(sampler.snapshot().serial == 0 && !sampler.snapshot().available);
  assert(!sampler.snapshot().sampledAt && !sampler.snapshot().rssBytes);
  assert(sampler.observe(start, ProcessResourceCounters{10.0, 4096, 8192}));
  assert(sampler.snapshot().serial == 1 && !sampler.snapshot().cpuPercent);
  assert(sampler.observe(start + 250ms,
                         ProcessResourceCounters{10.25, 4096, 8192}));
  assert(sampler.snapshot().cpuPercent == 100.0);
}

void platformSmokeTest() {
  ProcessResourceSampler sampler(1ms);
  assert(sampler.poll());
  const auto first = sampler.snapshot();
  assert(first.serial == 1 && first.sampledAt);
  assert(!first.cpuPercent);

#if defined(__linux__) || defined(__ANDROID__)
  assert(first.available);
  assert(first.rssBytes && *first.rssBytes > 0);
  assert(first.peakRssBytes && *first.peakRssBytes > 0);
  std::this_thread::sleep_for(5ms);
  assert(sampler.poll());
  const auto second = sampler.snapshot();
  assert(second.serial == 2 && second.sampledAt > first.sampledAt);
  assert(second.cpuPercent && std::isfinite(*second.cpuPercent) &&
         *second.cpuPercent >= 0.0);
  assert(second.peakRssBytes && *second.peakRssBytes >= *first.peakRssBytes);
#else
  assert(!first.available && !first.rssBytes && !first.peakRssBytes);
#endif
}

} // namespace

int main() {
  calculationTest();
  lifecycleTest();
  platformSmokeTest();
  std::cout << "process resource sampler checks passed\n";
}
