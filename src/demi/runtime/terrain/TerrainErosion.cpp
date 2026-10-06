#include "demi/runtime/terrain/TerrainErosion.h"

#include "demi/runtime/simulation/DeterministicRandom.h"
#include "demi/runtime/terrain/TerrainSeed.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
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
constexpr float ThermalDiagonal = 0.70710678F;
// The share of its own relief above its lowest neighbour that a cell may shed in
// one thermal pass. Without this a cell with several steeper neighbours gives
// each of them a full step's worth and ends up driven below its own ground.
constexpr float MaxShedFraction = 0.25F;

constexpr int NeighbourX[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int NeighbourZ[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
constexpr bool NeighbourDiagonal[8] = {true, false, true, false, false,
                                       true, false, true};

// One stream per droplet, derived from the Erosion seed channel and folded with
// the iteration and droplet index. Indexing by position rather than drawing from
// one running generator is what makes the result independent of traversal order:
// droplet 4000 walks the same path whether it is the fourth or the four
// thousandth of its iteration, and the whole stage can be replayed or parallelised
// without reshuffling the surface.
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

} // namespace

std::optional<TerrainSamples<float>>
applyTerrainErosion(const HeightField &field, const TerrainDrainage &drainage,
                    const TerrainErosionSettings &settings,
                    TerrainSamples<float> *sedimentOut, std::stop_token stop) {
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
      drainage.flow.size() != grid.count ||
      drainage.basin.size() != grid.count)
    throw std::invalid_argument(
        "Terrain erosion drainage does not match the heightfield");
  if (field.heights.empty())
    return std::nullopt;

  const auto count = grid.count;
  const bool preview = settings.quality == TerrainQuality::Preview;
  const float cellSize = std::min(field.size.x / float(std::max(1, field.cellsX)),
                                  field.size.y / float(std::max(1, field.cellsZ)));

  TerrainSamples<float> heights;
  heights.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    if ((i & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    heights.set(i, field.heights[i]);
  }

  // Preview buys its iteration speed with a smaller droplet population, a
  // shorter walk per droplet and no thermal pass at all. The factor is large
  // enough that the two tiers cannot be mistaken for one another.
  const int iterations =
      std::max(1, preview ? std::max(1, settings.iterations / 8)
                          : settings.iterations);
  const int droplets =
      preview ? std::clamp(int(count) / 8, 16, 256)
              : std::clamp(int(count) * iterations / 256, 64, 200000);
  const int maxSteps = preview ? 24 : 96;

  for (int iteration = 0; iteration < iterations; ++iteration)
    for (int droplet = 0; droplet < droplets; ++droplet) {
      if ((droplet & 63) == 0 && stop.stop_requested())
        return std::nullopt;
      auto random = dropletRandom(settings.seed, iteration,
                                  std::size_t(droplet));
      auto cell = std::size_t(random.integer(0, int(count) - 1));
      float water = 1.F + settings.rainRate;
      float sediment = 0.F;
      float speed = 1.F;
      for (int step = 0; step < maxSteps; ++step) {
        const auto next = drainage.downstream[cell];
        // An outlet or a sink: there is nothing lower to walk to.
        if (next == terrainNoDownstream || next >= count)
          break;
        const float from = heights[cell];
        const float drop = heights[next] - from;
        const float capacity = std::max(
            0.F, -drop * speed * water * settings.sedimentCapacity);
        if (sediment > capacity || drop > 0.F) {
          // Carrying more than this stretch can hold, or climbing into a filled
          // sink, so the excess settles here.
          const float amount = drop > 0.F ? std::min(drop, sediment)
                                          : (sediment - capacity) *
                                                settings.depositionRate;
          sediment -= amount;
          heights.set(cell, from + amount);
        } else {
          const float amount = std::min((capacity - sediment) * ErosionRate,
                                        -drop * MaxCutRate);
          sediment += amount;
          heights.set(cell, from - amount);
        }
        speed = std::sqrt(std::max(0.F, speed * speed - drop * Gravity));
        water *= 1.F - settings.evaporation;
        cell = next;
        if (water < MinimumWater)
          break;
      }
      // Whatever the droplet still carries settles where it came to rest.
      // Discarding it instead would make the stage destroy material, and the
      // net loss would grow with the droplet count rather than with the erosion.
      if (sediment > 0.F)
        heights.set(cell, heights[cell] + sediment);
    }

  // Thermal relaxation reads every neighbour from the pass input and writes into
  // a separate buffer. In-place relaxation would hand each cell neighbours that
  // had already moved, which biases transport towards whichever corner the scan
  // reaches first and reads as a prevailing wind.
  if (!preview && settings.thermalStrength > 0.F) {
    for (int pass = 0; pass < ThermalPasses; ++pass) {
      if (stop.stop_requested())
        return std::nullopt;
      const TerrainSamples<float> input = heights;
      TerrainSamples<float> output;
      output.resize(count);
      for (std::size_t i = 0; i < count; ++i) {
        if ((i & 255) == 0 && stop.stop_requested())
          return std::nullopt;
        output.set(i, input[i]);
      }
      // What a cell would shed to one neighbour, and what it may shed in total.
      // Both are read-only views of the pass input, so neither depends on the
      // order cells are visited in.
      const auto reachOf = [&](int neighbour) {
        return cellSize * (NeighbourDiagonal[neighbour] ? ThermalDiagonal : 1.F);
      };
      const auto excessOver = [&](std::size_t cell, std::size_t target,
                                 int neighbour) {
        const float difference = input[cell] - input[target];
        const float slope = difference / std::max(1e-6F, reachOf(neighbour));
        if (slope <= settings.talusSlope)
          return 0.F;
        // Never more than half the step, so a pair of cells cannot overshoot
        // past each other and oscillate between the two buffers.
        return std::min((slope - settings.talusSlope) * settings.thermalStrength *
                            reachOf(neighbour),
                        std::max(0.F, difference) * 0.5F);
      };
      std::vector<float> desired(count, 0.F);
      std::vector<float> allowed(count, 0.F);
      for (int z = 1; z < grid.depth - 1; ++z)
        for (int x = 1; x < grid.width - 1; ++x) {
          if ((x & 255) == 0 && stop.stop_requested())
            return std::nullopt;
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
          allowed[cell] = std::min(
              wanted, std::max(0.F, here - lowest) * MaxShedFraction);
        }
      for (int z = 1; z < grid.depth - 1; ++z)
        for (int x = 1; x < grid.width - 1; ++x) {
          if ((x & 255) == 0 && stop.stop_requested())
            return std::nullopt;
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
            output.set(cell, output[cell] - move);
            output.set(target, output[target] + move);
          }
        }
      heights = std::move(output);
    }
  }

  if (sedimentOut) {
    if (stop.stop_requested())
      return std::nullopt;
    TerrainSamples<float> signedSediment;
    signedSediment.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
      if ((i & 255) == 0 && stop.stop_requested())
        return std::nullopt;
      signedSediment.set(i, heights[i] - field.heights[i]);
    }
    if (stop.stop_requested())
      return std::nullopt;
    *sedimentOut = std::move(signedSediment);
  }
  if (stop.stop_requested())
    return std::nullopt;
  return heights;
}

} // namespace demi::runtime
