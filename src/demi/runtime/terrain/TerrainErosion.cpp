#include "demi/runtime/terrain/TerrainErosion.h"

#include "demi/runtime/simulation/DeterministicRandom.h"
#include "demi/runtime/terrain/TerrainSeed.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace demi::runtime {
namespace {

using simulation::DeterministicRandom;

constexpr float Gravity = 9.81F;
// Below this a droplet has dried out and continuing would just move sediment a
// few more cells for nothing.
constexpr float MinimumWater = 0.01F;
// The share of the local fall a droplet may cut in one step. It is small on
// purpose: a droplet that could take the whole fall in one visit would plan the
// slope off in a single pass, and the iteration count would stop meaning
// anything.
constexpr float ErosionRate = 0.3F;
constexpr float MaxCutRate = 0.05F;
constexpr int ThermalPasses = 6;
// The share of its own relief above its lowest neighbour that a cell may shed
// in one thermal pass. Without this a cell with several steeper neighbours
// gives each of them a full step's worth and ends up driven below its own
// ground.
constexpr float MaxShedFraction = 0.25F;

constexpr int NeighbourX[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int NeighbourZ[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// One stream per droplet, derived from the Erosion seed channel and folded with
// the iteration and droplet index. Indexing by position rather than drawing
// from one running generator is what makes the result independent of traversal
// order of random draws: each droplet gets a reproducible stream. Surface
// updates still run in a fixed serial order; parallel execution would change
// the surface seen by later droplets.
[[nodiscard]] DeterministicRandom dropletRandom(int worldSeed, int iteration,
                                                std::size_t droplet) {
  const auto channel =
      deriveTerrainSubSeed(worldSeed, TerrainSeedChannel::Erosion);
  std::uint64_t state = static_cast<std::uint64_t>(std::uint32_t(channel));
  state ^= static_cast<std::uint64_t>(std::uint32_t(iteration)) *
           0x9E3779B97F4A7C15ull;
  state ^= static_cast<std::uint64_t>(droplet) * 0xC2B2AE3D27D4EB4Full;
  return DeterministicRandom(state);
}

struct Grid {
  int width = 0;
  int depth = 0;
  std::size_t count = 0;

  [[nodiscard]] std::size_t at(int x, int z) const {
    return std::size_t(z) * std::size_t(width) + std::size_t(x);
  }
};

Grid gridOf(const HeightField &field) {
  Grid grid;
  grid.width = field.cellsX + 1;
  grid.depth = field.cellsZ + 1;
  grid.count = std::size_t(grid.width) * std::size_t(grid.depth);
  return grid;
}

bool isBorder(const Grid &grid, std::size_t cell) {
  const auto x = cell % std::size_t(grid.width);
  const auto z = cell / std::size_t(grid.width);
  return x == 0 || z == 0 || x + 1 == std::size_t(grid.width) ||
         z + 1 == std::size_t(grid.depth);
}

bool isMeshEdge(int neighbor) {
  return NeighbourX[neighbor] * NeighbourZ[neighbor] <= 0;
}

std::size_t chooseHydraulicNeighbor(const Grid &grid,
                                    const std::vector<float> &heights,
                                    const std::array<float, 8> &distances,
                                    std::size_t cell, std::size_t guidance) {
  const int x = int(cell % std::size_t(grid.width));
  const int z = int(cell / std::size_t(grid.width));
  auto chosen = terrainNoDownstream;
  double steepest = 0;
  for (int neighbor = 0; neighbor < 8; ++neighbor) {
    if (!isMeshEdge(neighbor)) {
      continue;
    }
    const auto target =
        grid.at(x + NeighbourX[neighbor], z + NeighbourZ[neighbor]);
    const double slope =
        (double(heights[cell]) - heights[target]) / distances[neighbor];
    const bool preferredTie =
        slope == steepest &&
        (target == guidance || (chosen != guidance && target < chosen));
    if (slope > steepest || (slope > 0 && preferredTie)) {
      steepest = slope;
      chosen = target;
    }
  }
  if (chosen != terrainNoDownstream) {
    return chosen;
  }
  // Flats may use a validated adjacent drainage edge. Uphill filled-tree
  // guidance cannot drain the current surface and is deliberately ignored.
  if (guidance < heights.size() && heights[guidance] == heights[cell]) {
    const int gx = int(guidance % std::size_t(grid.width));
    const int gz = int(guidance / std::size_t(grid.width));
    const int dx = gx - x;
    const int dz = gz - z;
    if ((dx != 0 || dz != 0) && std::abs(dx) <= 1 && std::abs(dz) <= 1 &&
        dx * dz <= 0) {
      return guidance;
    }
  }
  return terrainNoDownstream;
}

double depositWithinTalus(const Grid &grid, std::vector<float> &heights,
                          const std::array<float, 8> &distances,
                          std::size_t cell, double sediment, float talusSlope) {
  if (isBorder(grid, cell) || sediment <= 0) {
    return 0;
  }
  const int x = int(cell % std::size_t(grid.width));
  const int z = int(cell / std::size_t(grid.width));
  double ceiling = std::numeric_limits<double>::infinity();
  for (int neighbor = 0; neighbor < 8; ++neighbor) {
    if (!isMeshEdge(neighbor)) {
      continue;
    }
    const auto target =
        grid.at(x + NeighbourX[neighbor], z + NeighbourZ[neighbor]);
    ceiling = std::min(ceiling, double(heights[target]) +
                                    talusSlope * distances[neighbor]);
  }
  const double amount =
      std::min(sediment, std::max(0.0, ceiling - heights[cell]));
  const float raised = float(double(heights[cell]) + amount);
  const double actual = double(raised) - heights[cell];
  // Rounding must never withdraw more material than the droplet carries or
  // put the sample above its geometric capacity.
  if (actual <= 0 || actual > sediment || double(raised) > ceiling) {
    return 0;
  }
  heights[cell] = raised;
  return actual;
}

} // namespace

std::optional<TerrainSamples<float>>
applyTerrainErosion(const HeightField &field, const TerrainDrainage &drainage,
                    const TerrainErosionSettings &settings,
                    TerrainSamples<float> *sedimentOut, std::stop_token stop,
                    const TerrainGenerator::Progress &progress,
                    TerrainErosionTransport *transportOut) {
  if (stop.stop_requested())
    return std::nullopt;
  if (field.cellsX < 0 || field.cellsZ < 0)
    throw std::invalid_argument("Terrain erosion has a negative grid extent");
  const Grid grid = gridOf(field);
  if (field.heights.size() != grid.count)
    throw std::invalid_argument(
        "Terrain erosion does not match the heightfield");
  // A drainage computed from a different surface is a wrong-direction hazard
  // rather than a cosmetic mismatch, so it is refused up front.
  if (drainage.downstream.size() != grid.count ||
      drainage.flow.size() != grid.count || drainage.basin.size() != grid.count)
    throw std::invalid_argument(
        "Terrain erosion drainage does not match the heightfield");
  if (field.heights.empty())
    return std::nullopt;

  const bool validRates =
      std::isfinite(settings.rainRate) && settings.rainRate >= 0 &&
      std::isfinite(settings.sedimentCapacity) &&
      settings.sedimentCapacity >= 0 &&
      std::isfinite(settings.depositionRate) && settings.depositionRate >= 0 &&
      settings.depositionRate <= 1 && std::isfinite(settings.evaporation) &&
      settings.evaporation >= 0 && settings.evaporation <= 1 &&
      std::isfinite(settings.thermalStrength) &&
      settings.thermalStrength >= 0 && std::isfinite(settings.talusSlope) &&
      settings.talusSlope >= 0;
  if (!validRates || !std::isfinite(field.size.x) ||
      !std::isfinite(field.size.y) || field.size.x <= 0 || field.size.y <= 0) {
    throw std::invalid_argument("Terrain erosion requires finite dimensions "
                                "and physical transport rates");
  }

  const auto count = grid.count;
  if (count > std::size_t(std::numeric_limits<int>::max())) {
    throw std::invalid_argument(
        "Terrain erosion exceeds its random sample index range");
  }
  const bool preview = settings.quality == TerrainQuality::Preview;
  const float spacingX = field.size.x / float(std::max(1, field.cellsX));
  const float spacingZ = field.size.y / float(std::max(1, field.cellsZ));
  std::array<float, 8> neighborDistances;
  for (std::size_t neighbor = 0; neighbor < neighborDistances.size();
       ++neighbor) {
    // Talus is rise over physical run, including both axes on diagonal edges.
    neighborDistances[neighbor] = std::hypot(NeighbourX[neighbor] * spacingX,
                                             NeighbourZ[neighbor] * spacingZ);
    if (!std::isfinite(neighborDistances[neighbor]) ||
        neighborDistances[neighbor] <= 0) {
      throw std::invalid_argument(
          "Terrain erosion neighbor distances must be finite and positive");
    }
  }

  std::vector<float> heights(count);
  TerrainErosionTransport transport;
  for (std::size_t i = 0; i < count; ++i) {
    if ((i & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    if (!std::isfinite(field.heights[i])) {
      throw std::invalid_argument(
          "Terrain erosion requires finite surface heights");
    }
    heights[i] = field.heights[i];
  }

  // Preview buys its iteration speed with a smaller droplet population, a
  // shorter walk per droplet and no thermal pass at all. The factor is large
  // enough that the two tiers cannot be mistaken for one another.
  if (settings.iterations < 0) {
    throw std::invalid_argument(
        "Hydraulic erosion iterations must be nonnegative");
  }
  int iterations = settings.iterations;
  if (preview && iterations > 0) {
    iterations = std::max(1, iterations / 8);
  }
  const int droplets =
      preview
          ? std::clamp(int(count) / 8, 16, 256)
          : int(std::clamp(double(count) * iterations / 256.0, 64.0, 200000.0));
  const int maxSteps = preview ? 24 : 96;

  const float hydraulicFraction =
      !preview && settings.thermalStrength > 0.F ? 0.8F : 1.0F;
  for (int iteration = 0; iteration < iterations; ++iteration) {
    for (int droplet = 0; droplet < droplets; ++droplet) {
      if ((droplet & 63) == 0) {
        if (progress) {
          const double completed = double(iteration) * droplets + droplet;
          const double total = double(iterations) * droplets;
          progress(hydraulicFraction * float(completed / total));
        }
        if (stop.stop_requested()) {
          return std::nullopt;
        }
      }
      auto random =
          dropletRandom(settings.seed, iteration, std::size_t(droplet));
      auto cell = std::size_t(random.integer(0, int(count) - 1));
      double water = 1.0 + settings.rainRate;
      double sediment = 0;
      double speed = 1;
      for (int step = 0; step < maxSteps; ++step) {
        if (isBorder(grid, cell)) {
          transport.exportedSediment += sediment;
          sediment = 0;
          break;
        }
        const auto next = chooseHydraulicNeighbor(
            grid, heights, neighborDistances, cell, drainage.downstream[cell]);
        if (next == terrainNoDownstream) {
          break;
        }
        const double drop = double(heights[cell]) - heights[next];
        const double capacity =
            drop * speed * water * settings.sedimentCapacity;
        if (sediment > capacity) {
          const double deposited = depositWithinTalus(
              grid, heights, neighborDistances, cell,
              (sediment - capacity) * settings.depositionRate,
              settings.talusSlope);
          sediment -= deposited;
          transport.depositedSediment += deposited;
        } else {
          const double requested =
              std::min((capacity - sediment) * ErosionRate, drop * MaxCutRate);
          const float lowered = float(double(heights[cell]) - requested);
          const double actual = double(heights[cell]) - lowered;
          if (actual > 0 && lowered >= heights[next]) {
            heights[cell] = lowered;
            sediment += actual;
            transport.erodedSediment += actual;
          }
        }
        speed = std::sqrt(std::max(0.0, speed * speed + drop * Gravity));
        water *= 1.0 - settings.evaporation;
        cell = next;
        if (water < MinimumWater) {
          break;
        }
      }
      if (isBorder(grid, cell)) {
        transport.exportedSediment += sediment;
      } else if (sediment > 0) {
        // Terminal settling has bounded geometric capacity. Spread remaining
        // load over one mesh-edge ring; any excess stays explicitly suspended.
        const int x = int(cell % std::size_t(grid.width));
        const int z = int(cell / std::size_t(grid.width));
        const double deposited =
            depositWithinTalus(grid, heights, neighborDistances, cell, sediment,
                               settings.talusSlope);
        sediment -= deposited;
        transport.depositedSediment += deposited;
        for (int neighbor = 0; neighbor < 8 && sediment > 0; ++neighbor) {
          if (!isMeshEdge(neighbor)) {
            continue;
          }
          const auto target =
              grid.at(x + NeighbourX[neighbor], z + NeighbourZ[neighbor]);
          const double spread =
              depositWithinTalus(grid, heights, neighborDistances, target,
                                 sediment, settings.talusSlope);
          sediment -= spread;
          transport.depositedSediment += spread;
        }
        transport.suspendedSediment += sediment;
      }
    }
  }
  if (progress) {
    progress(hydraulicFraction);
  }
  if (stop.stop_requested()) {
    return std::nullopt;
  }

  // Thermal relaxation reads every neighbour from the pass input and writes
  // into a separate buffer. In-place relaxation would hand each cell neighbours
  // that had already moved, which biases transport towards whichever corner the
  // scan reaches first and reads as a prevailing wind.
  if (!preview && settings.thermalStrength > 0.F) {
    for (int pass = 0; pass < ThermalPasses; ++pass) {
      if (stop.stop_requested())
        return std::nullopt;
      const std::vector<float> input = heights;
      std::vector<float> output(count);
      for (std::size_t i = 0; i < count; ++i) {
        if ((i & 255) == 0 && stop.stop_requested())
          return std::nullopt;
        output[i] = input[i];
      }
      // What a cell would shed to one neighbour, and what it may shed in total.
      // Both are read-only views of the pass input, so neither depends on the
      // order cells are visited in.
      const auto reachOf = [&](int neighbour) {
        return neighborDistances[neighbour];
      };
      const auto excessOver = [&](std::size_t cell, std::size_t target,
                                  int neighbour) {
        const float difference = input[cell] - input[target];
        const float slope = difference / std::max(1e-6F, reachOf(neighbour));
        if (slope <= settings.talusSlope)
          return 0.F;
        // Never more than half the step, so a pair of cells cannot overshoot
        // past each other and oscillate between the two buffers.
        return std::min((slope - settings.talusSlope) *
                            settings.thermalStrength * reachOf(neighbour),
                        std::max(0.F, difference) * 0.5F);
      };
      std::vector<float> desired(count, 0.F);
      std::vector<float> allowed(count, 0.F);
      for (int z = 1; z < grid.depth - 1; ++z)
        for (int x = 1; x < grid.width - 1; ++x) {
          if ((x == 1 || (x & 255) == 0) && stop.stop_requested()) {
            return std::nullopt;
          }
          const auto cell = grid.at(x, z);
          const float here = input[cell];
          float wanted = 0.F;
          float lowest = here;
          for (int neighbour = 0; neighbour < 8; ++neighbour) {
            const auto target =
                grid.at(x + NeighbourX[neighbour], z + NeighbourZ[neighbour]);
            lowest = std::min(lowest, input[target]);
            wanted += excessOver(cell, target, neighbour);
          }
          desired[cell] = wanted;
          // The budget is what keeps the pass stable. A cell with eight steeper
          // neighbours wants eight steps' worth of movement and would otherwise
          // end up far below its own ground, dumping the difference on them.
          allowed[cell] =
              std::min(wanted, std::max(0.F, here - lowest) * MaxShedFraction);
        }
      for (int z = 1; z < grid.depth - 1; ++z)
        for (int x = 1; x < grid.width - 1; ++x) {
          if ((x == 1 || (x & 255) == 0) && stop.stop_requested()) {
            return std::nullopt;
          }
          const auto cell = grid.at(x, z);
          if (desired[cell] <= 0.F)
            continue;
          const auto share = allowed[cell] / desired[cell];
          for (int neighbour = 0; neighbour < 8; ++neighbour) {
            const auto target =
                grid.at(x + NeighbourX[neighbour], z + NeighbourZ[neighbour]);
            const float move = excessOver(cell, target, neighbour) * share;
            if (move <= 0.F)
              continue;
            output[cell] -= move;
            output[target] += move;
          }
        }
      heights = std::move(output);
      if (progress) {
        progress(hydraulicFraction +
                 (1.0F - hydraulicFraction) * float(pass + 1) / ThermalPasses);
      }
    }
  }

  TerrainSamples<float> result;
  TerrainSamples<float> signedSediment;
  result.resize(count);
  if (sedimentOut) {
    signedSediment.resize(count);
  }
  for (std::size_t sample = 0; sample < count; ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested()) {
      return std::nullopt;
    }
    const double delta = double(heights[sample]) - field.heights[sample];
    if (!std::isfinite(heights[sample]) || !std::isfinite(delta) ||
        std::abs(delta) > std::numeric_limits<float>::max()) {
      throw std::invalid_argument("Terrain erosion output and transport deltas "
                                  "must fit finite samples");
    }
    result.set(sample, heights[sample]);
    if (sedimentOut) {
      signedSediment.set(sample, heights[sample] - field.heights[sample]);
    }
  }
  if (progress) {
    progress(1.0F);
  }
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  if (sedimentOut) {
    *sedimentOut = std::move(signedSediment);
  }
  if (transportOut) {
    *transportOut = transport;
  }
  return result;
}

} // namespace demi::runtime
