#include "demi/assets/TerrainPlacementCodec.h"
#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainMasks.h"
#include "demi/runtime/terrain/TerrainScatter.h"
#include "demi/runtime/terrain/TerrainScatterConstraints.h"
#include "demi/runtime/terrain/TerrainScatterLayout.h"
#include "demi/runtime/terrain/TerrainVegetationResidency.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace allocation_probe {
struct Counts {
  std::size_t calls = 0;
  std::size_t requestedBytes = 0;
};
thread_local bool enabled = false;
thread_local Counts counts;

void *allocate(std::size_t bytes, std::size_t alignment = 0) {
  void *memory = nullptr;
  if (alignment == 0) {
    memory = std::malloc(std::max<std::size_t>(bytes, 1));
  } else if (posix_memalign(&memory, alignment,
                            std::max<std::size_t>(bytes, 1)) != 0) {
    memory = nullptr;
  }
  if (!memory) {
    throw std::bad_alloc();
  }
  if (enabled) {
    ++counts.calls;
    counts.requestedBytes += bytes;
  }
  return memory;
}
} // namespace allocation_probe

// Executable-local instrumentation counts C++ allocations on the measured
// thread, not malloc calls inside dependencies, peak live bytes or process RSS.
void *operator new(std::size_t bytes) {
  return allocation_probe::allocate(bytes);
}
void *operator new[](std::size_t bytes) {
  return allocation_probe::allocate(bytes);
}
void *operator new(std::size_t bytes, std::align_val_t alignment) {
  return allocation_probe::allocate(bytes, static_cast<std::size_t>(alignment));
}
void *operator new[](std::size_t bytes, std::align_val_t alignment) {
  return allocation_probe::allocate(bytes, static_cast<std::size_t>(alignment));
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept {
  std::free(memory);
}
void operator delete(void *memory, std::align_val_t) noexcept {
  std::free(memory);
}
void operator delete[](void *memory, std::align_val_t) noexcept {
  std::free(memory);
}
void operator delete(void *memory, std::size_t, std::align_val_t) noexcept {
  std::free(memory);
}
void operator delete[](void *memory, std::size_t, std::align_val_t) noexcept {
  std::free(memory);
}

namespace {
using namespace demi::runtime;
using Clock = std::chrono::steady_clock;

struct Inputs {
  TerrainRecipe recipe;
  HeightField field;
  TerrainPalette palette;
  std::vector<std::uint8_t> wet;
};

TerrainPaletteEntry rule(std::string prefab, TerrainScatterCategory category,
                         float density, float candidateSpacing, float spacing,
                         float clusterSize, float clusterStrength) {
  TerrainPaletteEntry entry;
  entry.prefab = std::move(prefab);
  entry.category = category;
  entry.density = density;
  entry.candidateSpacing = candidateSpacing;
  entry.positionJitter = 1;
  entry.spacing = spacing;
  entry.clusterSize = clusterSize;
  entry.clusterStrength = clusterStrength;
  entry.scaleMin = 0.7F;
  entry.scaleMax = 1.6F;
  entry.collision = TerrainCollisionPolicy::None;
  return entry;
}

Inputs makeInputs(int metres, float densityScale) {
  Inputs inputs;
  inputs.recipe.seed = 1706;
  inputs.recipe.size = {float(metres), float(metres)};
  inputs.recipe.cellsX = inputs.recipe.cellsZ = metres / 2;
  auto &field = inputs.field;
  field.size = inputs.recipe.size;
  field.cellsX = field.cellsZ = metres / 2;
  const auto count = std::size_t(field.cellsX + 1) * (field.cellsZ + 1);
  field.heights.resize(count);
  field.biomeIndices.resize(count);
  field.exclusions.resize(count);
  field.biomeIds = {"meadow", "rock"};
  inputs.wet.resize(count);
  for (int z = 0; z <= field.cellsZ; ++z) {
    for (int x = 0; x <= field.cellsX; ++x) {
      const auto index = field.index(x, z);
      const double px = double(x) * 2;
      const double pz = double(z) * 2;
      const bool wet =
          std::hypot(px - metres * 0.42, pz - metres * 0.48) < metres * 0.12;
      const double ridge = std::sin(px / 19.0 + std::sin(pz / 31.0));
      const float height =
          wet ? -2.F
              : float(20 + px * 0.025 +
                      3 * std::sin(px / 26.0) * std::cos(pz / 33.0) +
                      8 * ridge * ridge);
      field.heights.set(index, height);
      field.biomeIndices.set(index, ridge > 0.8 ? 1 : 0);
      field.exclusions.set(
          index,
          std::hypot(px - metres * 0.76, pz - metres * 0.28) < 5 ? 1.F : 0.F);
      inputs.wet[index] = wet;
    }
  }
  if (!TerrainGenerator::recomputeTerrainNormals(field)) {
    throw std::runtime_error("Cannot prepare benchmark normals");
  }
  inputs.palette.id = "asset://benchmark/mixed-vegetation";
  inputs.palette.artDensity = densityScale;
  auto grass = rule("prefab://benchmark/grass-patch",
                    TerrainScatterCategory::Grass, 0.78F, 1.1F, 0, 26, 0.62F);
  grass.slope = {0, 32};
  grass.moisture = {0.1F, 1};
  grass.waterDistance = {2, 10000};
  grass.biomes = {"meadow"};
  inputs.palette.placements.emplace("grass", grass);
  auto reeds = rule("prefab://benchmark/shoreline-reeds",
                    TerrainScatterCategory::Grass, 1.2F, 0.7F, 0, 9, 0.55F);
  reeds.waterDistance = {0, 6};
  reeds.slope = {0, 35};
  inputs.palette.placements.emplace("reeds", reeds);
  auto trees = rule("prefab://benchmark/woodland-tree",
                    TerrainScatterCategory::Tree, 0.035F, 4, 6, 48, 0.72F);
  trees.waterDistance = {9, 10000};
  trees.slope = {0, 35};
  inputs.palette.placements.emplace("trees", trees);
  auto rocks = rule("prefab://benchmark/rock-outcrop",
                    TerrainScatterCategory::Rock, 0.015F, 3, 5, 34, 0.8F);
  rocks.slope = {5, 45};
  inputs.palette.placements.emplace("rocks", rocks);
  field.paletteId = inputs.palette.id;
  return inputs;
}

struct Sample {
  double milliseconds = 0;
  double allocations = 0;
  double requestedBytes = 0;
};

struct RegenerationPhases {
  Sample constraints;
  Sample placements;
};

struct PhaseMark {
  Clock::time_point time;
  allocation_probe::Counts allocations;
};

PhaseMark phaseMark() { return {Clock::now(), allocation_probe::counts}; }

Sample phaseSample(const PhaseMark &begin, const PhaseMark &end) {
  return {
      std::chrono::duration<double, std::milli>(end.time - begin.time).count(),
      double(end.allocations.calls - begin.allocations.calls),
      double(end.allocations.requestedBytes -
             begin.allocations.requestedBytes)};
}

TerrainScatterResult regenerate(const Inputs &inputs,
                                RegenerationPhases *phases = nullptr,
                                std::stop_token stop = {}) {
  PhaseMark start{};
  if (phases) {
    start = phaseMark();
  }
  // Include the production environmental measurements in regeneration cost.
  // Passing prebuilt constraints would hide work a loaded prepared cell needs.
  const std::vector<float> heights(inputs.field.heights.begin(),
                                   inputs.field.heights.end());
  TerrainRuleContextBuilder context(inputs.recipe);
  context.build(inputs.recipe, heights, inputs.field.cellsX,
                inputs.field.cellsZ, inputs.field.size, &inputs.wet);
  TerrainMasks masks;
  masks.slope.resize(context.contexts().size());
  masks.moisture.resize(context.contexts().size());
  masks.waterDistance.resize(context.contexts().size());
  for (std::size_t index = 0; index < context.contexts().size(); ++index) {
    const auto &sample = context.contexts()[index];
    masks.slope.set(index, sample.slope);
    masks.moisture.set(index, sample.moisture);
    masks.waterDistance.set(index, sample.waterDistance);
  }
  auto constraints = TerrainScatterConstraints::build(
      inputs.field, inputs.recipe, &masks,
      {.seaLevel = std::numeric_limits<float>::lowest(), .authored = true});
  const float metres = inputs.field.size.x;
  constraints.addProtectedArea({metres * 0.2F, metres * 0.7F}, 6);
  constraints.addRoad({{0, 0, metres * 0.2F},
                       {metres * 0.5F, 0, metres * 0.3F},
                       {metres, 0, metres * 0.2F}},
                      3);
  constraints.addFootprint({{metres * 0.7F, metres * 0.7F},
                            {metres * 0.7F + 8, metres * 0.7F},
                            {metres * 0.7F + 8, metres * 0.7F + 8},
                            {metres * 0.7F, metres * 0.7F + 8}});
  PhaseMark placementStart{};
  if (phases) {
    placementStart = phaseMark();
    phases->constraints = phaseSample(start, placementStart);
  }
  auto result = scatterTerrain(inputs.field, inputs.recipe, inputs.palette, {},
                               &constraints, inputs.wet, stop);
  if (phases) {
    phases->placements = phaseSample(placementStart, phaseMark());
  }
  if (result.truncated) {
    throw std::runtime_error(
        "Benchmark scatter truncated: comparison is invalid");
  }
  return result;
}

bool sameFloat(float first, float second) {
  return std::bit_cast<std::uint32_t>(first) ==
         std::bit_cast<std::uint32_t>(second);
}

void verify(const std::vector<TerrainScatterPlacement> &actual,
            const std::vector<TerrainScatterPlacement> &expected) {
  if (actual.size() != expected.size()) {
    throw std::runtime_error("Placement count differs");
  }
  for (std::size_t index = 0; index < expected.size(); ++index) {
    const auto &a = actual[index];
    const auto &b = expected[index];
    if (a.ruleId != b.ruleId || a.model != b.model || a.prefab != b.prefab ||
        a.biome != b.biome || a.biomeName != b.biomeName || a.cell != b.cell ||
        a.candidateX != b.candidateX || a.candidateZ != b.candidateZ ||
        a.collision != b.collision || !sameFloat(a.position.x, b.position.x) ||
        !sameFloat(a.position.y, b.position.y) ||
        !sameFloat(a.position.z, b.position.z) || !sameFloat(a.yaw, b.yaw) ||
        !sameFloat(a.scale, b.scale)) {
      throw std::runtime_error("Placement content/identity differs at " +
                               std::to_string(index));
    }
  }
}

std::uint64_t
identityDigest(const Inputs &inputs,
               const std::vector<TerrainScatterPlacement> &values) {
  std::uint64_t digest = 14695981039346656037ULL;
  for (const auto &value : values) {
    const auto id = terrainScatterInstanceKey(
        inputs.palette.id, value.ruleId, value.candidateX, value.candidateZ);
    for (const unsigned char character : id) {
      digest ^= character;
      digest *= 1099511628211ULL;
    }
    digest ^= 0;
    digest *= 1099511628211ULL;
  }
  return digest;
}

template <class Operation> Sample measure(Operation operation) {
  allocation_probe::counts = {};
  allocation_probe::enabled = true;
  const auto start = Clock::now();
  try {
    operation();
  } catch (...) {
    allocation_probe::enabled = false;
    throw;
  }
  const auto end = Clock::now();
  allocation_probe::enabled = false;
  return {std::chrono::duration<double, std::milli>(end - start).count(),
          double(allocation_probe::counts.calls),
          double(allocation_probe::counts.requestedBytes)};
}

double quantile(std::vector<double> values, double fraction) {
  std::ranges::sort(values);
  return values[std::min(
      values.size() - 1,
      std::size_t(std::ceil(fraction * double(values.size()))) - 1)];
}

void report(int metres, float density, bool checkpoint,
            std::string_view operation, const std::vector<Sample> &samples,
            std::size_t placements, std::size_t binaryBytes,
            std::uint64_t digest) {
  std::vector<double> times;
  std::vector<double> allocations;
  std::vector<double> bytes;
  for (const auto &sample : samples) {
    times.push_back(sample.milliseconds);
    allocations.push_back(sample.allocations);
    bytes.push_back(sample.requestedBytes);
  }
  std::cout << metres << ',' << density << ',' << checkpoint << ',' << operation
            << ',' << samples.size() << ',' << placements << ',' << binaryBytes
            << ',' << sizeof(TerrainScatterPlacement) << ','
            << quantile(times, .5) << ',' << quantile(times, .95) << ','
            << quantile(allocations, .5) << ',' << quantile(allocations, .95)
            << ',' << quantile(bytes, .5) << ',' << quantile(bytes, .95) << ','
            << digest << '\n';
}

void benchmark(int metres, float density, int repetitions, bool includePhases) {
  const auto inputs = makeInputs(metres, density);
  auto baseline = regenerate(inputs);
  if (baseline.placements.empty()) {
    throw std::runtime_error("Benchmark fixture produced no placements");
  }
  const auto digest = identityDigest(inputs, baseline.placements);
  auto prepared = inputs.field;
  prepared.scatterPlacements = baseline.placements;
  for (const bool checkpoint : {false, true}) {
    if (checkpoint) {
      auto artifacts = std::make_shared<TerrainGraphArtifacts>();
      artifacts->basePlacements =
          std::make_shared<const std::vector<TerrainScatterPlacement>>(
              baseline.placements);
      prepared.graphArtifacts = std::move(artifacts);
    }
    const auto packed =
        demi::assets::terrain_payload::encodePlacements(prepared);
    std::vector<Sample> regeneration;
    std::vector<Sample> decoding;
    for (int iteration = -2; iteration < repetitions; ++iteration) {
      TerrainScatterResult regenerated;
      std::shared_ptr<const std::vector<TerrainScatterPlacement>>
          regeneratedCandidates;
      auto decoded = inputs.field;
      if (checkpoint) {
        decoded.graphArtifacts = std::make_shared<TerrainGraphArtifacts>();
      }
      Sample regenerateSample;
      Sample decodeSample;
      const auto runRegeneration = [&] {
        regenerateSample = measure([&] {
          regenerated = regenerate(inputs);
          // An editing checkpoint retains the immutable candidate set as well.
          if (checkpoint) {
            regeneratedCandidates =
                std::make_shared<const std::vector<TerrainScatterPlacement>>(
                    regenerated.placements);
          }
        });
      };
      const auto runDecode = [&] {
        decodeSample = measure([&] {
          demi::assets::terrain_payload::decodePlacements(packed, decoded);
        });
      };
      // Alternate order to reduce a fixed warm-cache/frequency ordering bias.
      if ((iteration & 1) == 0) {
        runRegeneration();
        runDecode();
      } else {
        runDecode();
        runRegeneration();
      }
      verify(regenerated.placements, baseline.placements);
      verify(decoded.scatterPlacements, baseline.placements);
      if (checkpoint) {
        verify(*decoded.graphArtifacts->basePlacements, baseline.placements);
        verify(*regeneratedCandidates, baseline.placements);
      }
      if (iteration >= 0) {
        regeneration.push_back(regenerateSample);
        decoding.push_back(decodeSample);
      }
    }
    report(metres, density, checkpoint, "regenerate_with_constraints",
           regeneration, baseline.placements.size(), packed.size(), digest);
    report(metres, density, checkpoint, "decode_compact13", decoding,
           baseline.placements.size(), packed.size(), digest);
    if (includePhases) {
      // Separate runs keep phase clocks out of the comparable full-cost rows.
      // Phases expose construction/placement costs, not teardown or IO costs.
      std::vector<Sample> constraints;
      std::vector<Sample> placements;
      std::vector<Sample> checkpointCopies;
      for (int iteration = -2; iteration < repetitions; ++iteration) {
        RegenerationPhases phases;
        TerrainScatterResult regenerated;
        (void)measure([&] { regenerated = regenerate(inputs, &phases); });
        verify(regenerated.placements, baseline.placements);
        Sample checkpointCopy;
        if (checkpoint) {
          std::shared_ptr<const std::vector<TerrainScatterPlacement>> retained;
          checkpointCopy = measure([&] {
            retained =
                std::make_shared<const std::vector<TerrainScatterPlacement>>(
                    regenerated.placements);
          });
          verify(*retained, baseline.placements);
        }
        if (iteration >= 0) {
          constraints.push_back(phases.constraints);
          placements.push_back(phases.placements);
          if (checkpoint) {
            checkpointCopies.push_back(checkpointCopy);
          }
        }
      }
      report(metres, density, checkpoint, "phase_constraints_build",
             constraints, baseline.placements.size(), packed.size(), digest);
      report(metres, density, checkpoint, "phase_placement_only", placements,
             baseline.placements.size(), packed.size(), digest);
      if (checkpoint) {
        report(metres, density, checkpoint, "phase_checkpoint_copy",
               checkpointCopies, baseline.placements.size(), packed.size(),
               digest);
      }
    }
  }
}

// This isolates lifecycle overhead using the same prepared fixture in each
// logical cell. It is not a regional geography or rendered traversal test.
void benchmarkResidency(int metres, float density, int repetitions) {
  const auto inputs = makeInputs(metres, density);
  const auto baseline = regenerate(inputs);
  auto field = inputs.field;
  field.scatterPlacements = baseline.placements;
  const auto packed = demi::assets::terrain_payload::encodePlacements(field);
  field.scatterPlacements.clear();
  field.scatterPlacements.shrink_to_fit();
  JobSystem jobs(1);
  using Residency = TerrainVegetationResidency;
  for (const auto mode : {TerrainVegetationStorage::Stored,
                          TerrainVegetationStorage::Procedural}) {
    std::atomic<std::int64_t> workerNanoseconds = 0;
    std::atomic<std::size_t> loads = 0;
    constexpr std::size_t reserve = 128U * 1024U * 1024U;
    Residency cells(
        jobs, {.maximumBytes = reserve * 3, .maximumPendingJobs = 1},
        [&](const auto &key, std::stop_token stop) {
          const auto begin = Clock::now();
          TerrainScatterResult result;
          if (key.storage == TerrainVegetationStorage::Procedural) {
            result = regenerate(inputs, nullptr, stop);
          } else {
            auto decoded = field;
            demi::assets::terrain_payload::decodePlacements(packed, decoded);
            result.placements = std::move(decoded.scatterPlacements);
          }
          workerNanoseconds +=
              std::chrono::duration_cast<std::chrono::nanoseconds>(
                  Clock::now() - begin)
                  .count();
          ++loads;
          return result;
        });
    std::vector<double> updateMilliseconds;
    std::size_t peakResident = 0;
    std::size_t peakPending = 0;
    std::size_t peakRetiring = 0;
    const auto update = [&](std::span<const Residency::Demand> demand) {
      const auto begin = Clock::now();
      cells.update(demand);
      updateMilliseconds.push_back(
          std::chrono::duration<double, std::milli>(Clock::now() - begin)
              .count());
      const auto stats = cells.statistics();
      peakResident = std::max(peakResident, stats.residentBytes);
      peakPending = std::max(peakPending, stats.pendingBytes);
      peakRetiring = std::max(peakRetiring, stats.retiringBytes);
      if (stats.residentBytes + stats.pendingBytes + stats.retiringBytes >
          reserve * 3) {
        throw std::runtime_error("Residency accounting exceeded budget");
      }
    };
    for (int repetition = 0; repetition < repetitions; ++repetition) {
      // Overlap, forward traversal, reverse and revisit. No permanent cache
      // of every cell visited: only the current three-cell window is retained.
      for (const int centre : {0, 1, 2, 3, 2, 1, 0}) {
        std::vector<Residency::Demand> demand;
        for (int offset = -1; offset <= 1; ++offset) {
          demand.push_back({{centre + offset, 0, 1, mode}, reserve});
        }
        const auto deadline = Clock::now() + std::chrono::seconds(30);
        for (;;) {
          update(demand);
          bool ready = true;
          for (const auto &request : demand) {
            if (!cells.error(request.key).empty()) {
              throw std::runtime_error(cells.error(request.key));
            }
            ready = ready && cells.find(request.key) != nullptr;
          }
          if (ready) {
            break;
          }
          if (Clock::now() >= deadline) {
            throw std::runtime_error("Residency benchmark timed out");
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (const auto &request : demand) {
          verify(*cells.find(request.key), baseline.placements);
        }
        const auto before = loads.load();
        for (int frame = 0; frame < 30; ++frame) {
          update(demand);
        }
        if (loads != before) {
          throw std::runtime_error(
              "Resident cells regenerated during steady updates");
        }
      }
      const auto retirementDeadline = Clock::now() + std::chrono::seconds(30);
      do {
        update({});
        if (Clock::now() >= retirementDeadline) {
          throw std::runtime_error("Retirement benchmark timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } while (cells.statistics().retiringJobs != 0);
      if (cells.statistics().residentBytes != 0 ||
          cells.statistics().pendingBytes != 0) {
        throw std::runtime_error("Cell eviction retained owned payload");
      }
    }
    std::sort(updateMilliseconds.begin(), updateMilliseconds.end());
    const auto p95 =
        updateMilliseconds[(updateMilliseconds.size() - 1) * 95 / 100];
    std::cout << metres << ','
              << (mode == TerrainVegetationStorage::Stored ? "stored"
                                                           : "procedural")
              << ',' << repetitions << ',' << loads << ','
              << double(workerNanoseconds.load()) / 1e6 / double(loads.load())
              << ',' << p95 << ',' << updateMilliseconds.back() << ','
              << peakResident << ',' << peakPending << ',' << peakRetiring
              << ',' << cells.statistics().residentBytes << '\n';
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    int repetitions = 9;
    int onlySize = 0;
    bool includePhases = false;
    bool residency = false;
    float density = 1.F;
    for (int index = 1; index < argc; ++index) {
      const std::string_view option = argv[index];
      if (option == "--help") {
        std::cout
            << "Usage: demi-terrain-scatter-benchmark [--size 128|256|512] "
               "[--repetitions N] [--density-scale (0,1]] [--phases] "
               "[--residency]\n";
        return 0;
      }
      if (option == "--residency") {
        residency = true;
        continue;
      }
      if (option == "--phases") {
        includePhases = true;
        continue;
      }
      if (++index >= argc) {
        throw std::invalid_argument("Missing benchmark option value");
      }
      if (option == "--size") {
        onlySize = std::stoi(argv[index]);
      } else if (option == "--repetitions") {
        repetitions = std::stoi(argv[index]);
      } else if (option == "--density-scale") {
        density = std::stof(argv[index]);
      } else {
        throw std::invalid_argument("Unknown benchmark option");
      }
    }
    if (repetitions < 1 || repetitions > 1000 || !std::isfinite(density) ||
        density <= 0 || density > 1 ||
        (onlySize != 0 && onlySize != 128 && onlySize != 256 &&
         onlySize != 512)) {
      throw std::invalid_argument(
          "Invalid size, repetition count or density scale");
    }
    if (residency) {
      std::cerr << "Prepared-fixture residency probe; one worker, no disk IO, "
                   "terrain sampling, GPU upload or collision publication. "
                   "Bytes are owned payload/reservations, not RSS.\n";
      std::cout
          << "cell_metres,mode,repetitions,loads,worker_mean_ms,"
             "update_p95_ms,update_max_ms,peak_resident_bytes,"
             "peak_reserved_bytes,peak_retiring_bytes,final_resident_bytes\n";
      for (const int metres : {128, 256, 512}) {
        if (onlySize == 0 || onlySize == metres) {
          benchmarkResidency(metres, density, repetitions);
        }
      }
      return 0;
    }
    std::cerr
        << "Scatter benchmark: 2m surface samples; CPU includes constraints, "
           "moisture/water distance, spatial exclusions and spacing.\n"
           "Two warmups per case; C++ allocations/requested bytes only, "
           "not malloc/RSS/peak live memory. Surface generation and disk IO "
           "excluded from BOTH paths. Output destruction and exact validation "
           "are outside timing. Record build configuration separately.\n";
    if (includePhases) {
      std::cerr << "Phase rows use separate runs and omit teardown. Full "
                   "regeneration "
                   "rows remain the end-to-end authority; phase medians need "
                   "not sum.\n";
    }
    std::cout
        << std::fixed << std::setprecision(6)
        << "cell_metres,density_scale,editing_checkpoint,operation,samples,"
           "placements,binary_bytes,native_record_bytes,cpu_median_ms,cpu_p95_"
           "ms,"
           "cxx_allocations_median,cxx_allocations_p95,requested_bytes_median,"
           "requested_bytes_p95,stable_identity_digest\n";
    for (const int metres : {128, 256, 512}) {
      if (onlySize == 0 || onlySize == metres) {
        benchmark(metres, density, repetitions, includePhases);
      }
    }
  } catch (const std::exception &error) {
    std::cerr << "Terrain scatter benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
