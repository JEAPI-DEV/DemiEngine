#include "demi/runtime/concurrency/JobSystem.h"

#include <atomic>
#include <cassert>
#include <stdexcept>
#include <vector>

using demi::runtime::JobSystem;

int main() {
  // Inline mode provides identical semantics on single-core/headless hosts.
  JobSystem inlineJobs(0);
  int inlineValue = 0;
  const auto inlineHandle = inlineJobs.submit([&] { inlineValue = 7; });
  assert(inlineHandle.ready());
  inlineHandle.wait();
  assert(inlineValue == 7);

  JobSystem jobs(3);
  std::vector<int> values(257, -1);
  jobs.parallelFor(values.size(), 17,
                   [&](const std::size_t index) {
                     values[index] = static_cast<int>(index * 2);
                   });
  for (std::size_t index = 0; index < values.size(); ++index)
    assert(values[index] == static_cast<int>(index * 2));

  bool surfaced = false;
  const auto failing = jobs.submit([] { throw std::runtime_error("job failed"); });
  try {
    failing.wait();
  } catch (const std::runtime_error &) {
    surfaced = true;
  }
  assert(surfaced);

  // A throwing chunk still waits for every sibling, avoiding dangling
  // references to the caller's indexed output.
  std::atomic<int> completed = 0;
  std::atomic<bool> completedLastChunk = false;
  surfaced = false;
  try {
    jobs.parallelFor(64, 8, [&](const std::size_t index) {
      if (index == 0)
        throw std::runtime_error("parallel failure");
      if (index == 63)
        completedLastChunk = true;
      ++completed;
    });
  } catch (const std::runtime_error &) {
    surfaced = true;
  }
  assert(surfaced);
  assert(completedLastChunk);
  assert(completed >= 48);

  jobs.shutdown();
  jobs.shutdown();
  bool rejected = false;
  try {
    static_cast<void>(jobs.submit([] {}));
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  assert(rejected);
  return 0;
}
