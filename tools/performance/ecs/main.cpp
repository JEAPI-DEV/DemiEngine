#include "EnTTWorld.h"
#include "NativeWorld.h"

#if defined(__linux__)
#include <sys/resource.h>
#endif
#include <algorithm>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numeric>
#include <stdexcept>
#include <string_view>

using namespace demi::evaluation;
using Clock = std::chrono::steady_clock;

namespace {
double milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void verify() {
  NativeWorld native(2048);
  EnTTWorld dense(2048);
  const auto compare = [&] {
    require(native.prepare(true) == dense.prepare(true),
            "Change-set membership differs");
    const auto &left = native.records();
    const auto &right = dense.records();
    for (std::size_t index = 0; index < left.size(); ++index) {
      const auto &a = left[index];
      const auto &b = right[index];
      require(a.x == b.x && a.y == b.y && a.z == b.z && a.radius == b.radius &&
                  a.visible == b.visible,
              "Persistent rendering records differ");
    }
    require(drawChecksum(left) == drawChecksum(right),
            "Visible output differs");
  };
  compare();
  require(native.prepare(true) == 0 && dense.prepare(true) == 0,
          "An unchanged world must not rebuild render records");
  for (std::size_t index = 0; index < 2048; index += 3) {
    native.move(index);
    dense.move(index);
    native.move(index);
    dense.move(index);
  }
  compare();
  for (std::size_t index = 0; index < 2048; index += 5) {
    native.toggleMesh(index);
    dense.toggleMesh(index);
  }
  compare();
  for (std::size_t index = 0; index < 2048; index += 7) {
    native.recreate(index);
    dense.recreate(index);
  }
  compare();
  native.prepare(false);
  dense.prepare(false);
  compare();
  std::cout
      << "PASS: initial projection, unchanged frames, coalesced edits, "
         "component removal/addition, entity recreation and scan agreement\n";
}

nlohmann::json summarize(std::vector<double> samples) {
  std::sort(samples.begin(), samples.end());
  return {{"median_ms", samples[samples.size() / 2]},
          {"p95_ms", samples[(samples.size() - 1) * 95 / 100]},
          {"mean_ms", std::accumulate(samples.begin(), samples.end(), 0.0) /
                          double(samples.size())}};
}

template <class World>
void measure(std::string_view backend, std::string_view workload,
             std::size_t count, int frames, bool tracked) {
  const auto creation = Clock::now();
  World world(count);
  world.prepare(tracked);
  const auto creationMs = milliseconds(creation);
  std::vector<double> mutations;
  std::vector<double> preparation;
  std::vector<double> visibility;
  std::vector<double> totals;
  std::uint64_t checksum = 0;
  std::size_t visited = 0;
  // Warmup uses the same mutations for every backend. Timed samples exclude it.
  for (int frame = -10; frame < frames; ++frame) {
    const auto totalStart = Clock::now();
    const auto mutationStart = Clock::now();
    const std::size_t stride = workload == "moving_all" ? 1 : 100;
    if (workload != "static") {
      for (std::size_t index = 0; index < count; index += stride) {
        if (workload == "structural") {
          world.toggleMesh(index);
        } else {
          world.move(index);
        }
      }
    }
    const auto mutationMs = milliseconds(mutationStart);
    const auto preparationStart = Clock::now();
    const auto prepared = world.prepare(tracked);
    const auto preparationMs = milliseconds(preparationStart);
    const auto visibilityStart = Clock::now();
    const auto output = drawChecksum(world.records());
    const auto visibilityMs = milliseconds(visibilityStart);
    const auto totalMs = milliseconds(totalStart);
    if (frame >= 0) {
      mutations.push_back(mutationMs);
      preparation.push_back(preparationMs);
      visibility.push_back(visibilityMs);
      totals.push_back(totalMs);
      checksum += output;
      visited += prepared;
    }
  }
  long peakRssKiB = 0;
#if defined(__linux__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    peakRssKiB = usage.ru_maxrss;
  }
#endif
  std::cout << nlohmann::json{{"peak_rss_kib", peakRssKiB}, {"backend", backend},
                              {"tracked", tracked},
                              {"workload", workload},
                              {"entities", count},
                              {"frames", frames},
                              {"creation_ms", creationMs},
                              {"mutation", summarize(mutations)},
                              {"preparation", summarize(preparation)},
                              {"visibility", summarize(visibility)},
                              {"total", summarize(totals)},
                              {"projected_records", visited},
                              {"checksum", checksum}}
                   .dump()
            << '\n';
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--verify") {
      verify();
      return 0;
    }
    if (argc != 6) {
      std::cerr << "usage: demi-ecs-evaluation native|entt scan|tracked "
                   "static|moving_1pct|moving_all|structural count frames\n";
      return 2;
    }
    const std::string_view backend = argv[1];
    const std::string_view mode = argv[2];
    const std::string_view workload = argv[3];
    const auto count = std::stoull(argv[4]);
    const auto frames = std::stoi(argv[5]);
    require(backend == "native" || backend == "entt", "Unknown backend");
    require(mode == "scan" || mode == "tracked", "Unknown preparation mode");
    require(workload == "static" || workload == "moving_1pct" ||
                workload == "moving_all" || workload == "structural",
            "Unknown workload");
    require(count > 0 && count <= 1000000 && frames > 0, "Invalid sample size");
    if (backend == "native") {
      measure<NativeWorld>(backend, workload, count, frames, mode == "tracked");
    } else {
      measure<EnTTWorld>(backend, workload, count, frames, mode == "tracked");
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
