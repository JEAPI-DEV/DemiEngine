#include "demi/runtime/terrain/TerrainDrainage.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace demi::runtime {
namespace {

// Matches the chamfer weight in TerrainBiomeRules.cpp. The two transforms must
// agree: the rules band on water distance and drainage supplies the same mask,
// so a drift between them would let a rule match a distance the drainage stage
// never measured.
constexpr float DiagonalWeight = 0.70710678F;

// Raised on every level a flood closes. It is what makes a flat region and a
// closed basin terminate with a well-defined direction: without it a perfectly
// flat field would offer every cell to its neighbours at the same level with no
// downhill step, and the drainage tree would have cycles in it.
constexpr float FloodEpsilon = 1e-5F;

constexpr int NeighbourX[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int NeighbourZ[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
constexpr bool NeighbourDiagonal[8] = {true, false, true, false, false,
                                       true, false, true};

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

[[nodiscard]] bool isBorder(const Grid &grid, int x, int z) {
  return x == 0 || z == 0 || x == grid.width - 1 || z == grid.depth - 1;
}

// The multi-source chamfer distance transform, reproduced from
// TerrainBiomeRules.cpp so the rule condition and the drainage mask are the same
// measurement rather than two similar-looking ones.
std::vector<float> waterDistances(const std::vector<float> &heights,
                                  const Grid &grid, Vec2 size, float seaLevel,
                                  std::stop_token stop) {
  constexpr float Unreachable = std::numeric_limits<float>::max() / 4.F;
  std::vector<float> distance(grid.count, std::numeric_limits<float>::max());
  std::queue<std::size_t> frontier;
  for (int z = 0; z < grid.depth; ++z) {
    if (stop.stop_requested())
      return {};
    for (int x = 0; x < grid.width; ++x)
      if (heights[grid.at(x, z)] <= seaLevel) {
        const auto cell = grid.at(x, z);
        distance[cell] = 0;
        frontier.push(cell);
      }
  }
  if (frontier.empty()) {
    // No water anywhere: every sample is maximally far from it, so a
    // water-distance rule cannot accidentally match a narrow band.
    return std::vector<float>(grid.count, Unreachable);
  }
  const float stepX = size.x / float(grid.width - 1);
  const float stepZ = size.y / float(grid.depth - 1);
  const float diagonal = std::min(stepX, stepZ) * DiagonalWeight;
  const float straight = std::min(stepX, stepZ);
  while (!frontier.empty()) {
    if (stop.stop_requested())
      return {};
    const auto current = frontier.front();
    frontier.pop();
    const int x = int(current % std::size_t(grid.width));
    const int z = int(current / std::size_t(grid.width));
    for (int neighbour = 0; neighbour < 8; ++neighbour) {
      const int nx = x + NeighbourX[neighbour];
      const int nz = z + NeighbourZ[neighbour];
      if (nx < 0 || nz < 0 || nx >= grid.width || nz >= grid.depth)
        continue;
      const auto target = grid.at(nx, nz);
      const float step = NeighbourDiagonal[neighbour] ? diagonal : straight;
      if (distance[target] > distance[current] + step) {
        distance[target] = distance[current] + step;
        frontier.push(target);
      }
    }
  }
  return distance;
}

struct FloodNode {
  float level = 0;
  std::size_t index = 0;
};

// A strict weak ordering on (level, index). The index tiebreak is not cosmetic:
// without it a perfectly flat field would leave the heap's pop order among
// equal levels unspecified, and the basin a cell lands in would depend on the
// allocator rather than on the surface.
struct FloodGreater {
  bool operator()(const FloodNode &a, const FloodNode &b) const {
    if (a.level != b.level)
      return a.level > b.level;
    return a.index > b.index;
  }
};

} // namespace

std::optional<TerrainDrainage>
computeTerrainDrainage(const HeightField &field, const TerrainRecipe &recipe,
                       const TerrainDrainageSettings &settings,
                       std::stop_token stop) {
  if (stop.stop_requested())
    return std::nullopt;
  if (field.cellsX < 0 || field.cellsZ < 0)
    throw std::invalid_argument("Terrain drainage has a negative grid extent");
  if (recipe.cellsX != field.cellsX || recipe.cellsZ != field.cellsZ)
    throw std::invalid_argument(
        "Terrain drainage recipe does not match the heightfield");
  const Grid grid = gridOf(field);
  if (field.heights.size() != grid.count)
    throw std::invalid_argument(
        "Terrain drainage does not match the heightfield");
  if (field.heights.empty())
    return std::nullopt;

  std::vector<float> heights(grid.count);
  for (std::size_t i = 0; i < grid.count; ++i) {
    if ((i & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    heights[i] = field.heights[i];
  }

  // Priority flood (Barnes, Lehman & Mulla). The border is closed first and the
  // frontier is drained in increasing filled elevation, so each cell is reached
  // from the lowest neighbour that can still reach it and the result is the
  // same on every platform and every run.
  //
  // Termination does not depend on the surface being well behaved. The closed
  // flag admits each cell to the heap exactly once, so the loop runs at most
  // grid.count times whatever the heights are; the epsilon keeps every level
  // strictly above the level it was closed from, which is what stops a flat
  // region or a closed basin from forming a cycle in the drainage tree.
  std::priority_queue<FloodNode, std::vector<FloodNode>, FloodGreater> frontier;
  std::vector<char> closed(grid.count, 0);
  std::vector<std::size_t> parent(grid.count, terrainNoDownstream);
  // The level each sample was closed at. The gap between this and the sample's
  // own height is what identifies a depression.
  std::vector<float> filled(grid.count, 0.F);
  // Every sample is one unit of upstream area, including the border's.
  std::vector<double> accumulation(grid.count, 1.0);

  for (int z = 0; z < grid.depth; ++z) {
    if (stop.stop_requested())
      return std::nullopt;
    for (int x = 0; x < grid.width; ++x) {
      if (!isBorder(grid, x, z))
        continue;
      const auto cell = grid.at(x, z);
      closed[cell] = 1;
      filled[cell] = heights[cell];
      frontier.push({heights[cell], cell});
    }
  }

  while (!frontier.empty()) {
    if (stop.stop_requested())
      return std::nullopt;
    const FloodNode node = frontier.top();
    frontier.pop();
    // This cell's own upstream area is final here: everything draining into it
    // was closed at a strictly lower level and has already been popped. It is
    // added to the cell it drains into, which is the one that found it.
    if (parent[node.index] != terrainNoDownstream)
      accumulation[parent[node.index]] += accumulation[node.index];
    const int x = int(node.index % std::size_t(grid.width));
    const int z = int(node.index / std::size_t(grid.width));
    for (int neighbour = 0; neighbour < 8; ++neighbour) {
      const int nx = x + NeighbourX[neighbour];
      const int nz = z + NeighbourZ[neighbour];
      if (nx < 0 || nz < 0 || nx >= grid.width || nz >= grid.depth)
        continue;
      const auto target = grid.at(nx, nz);
      if (closed[target])
        continue;
      // First to close this cell owns it, which is what gives every sample
      // exactly one downstream neighbour even where the surface is flat.
      closed[target] = 1;
      parent[target] = node.index;
      filled[target] = std::max(node.level, heights[target]) + FloodEpsilon;
      frontier.push({filled[target], target});
    }
  }

  // Defensive: a group the border flood could not reach would otherwise be
  // reported as draining off the edge. It is forced into its own basin so a gap
  // can never masquerade as an outlet. A grid is fully connected under the
  // neighbour set, so this cannot trigger for a well formed field.
  std::vector<char> pooled(grid.count, 0);
  for (std::size_t start = 0; start < grid.count; ++start) {
    if ((start & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    if (closed[start])
      continue;
    std::queue<std::size_t> group;
    group.push(start);
    closed[start] = 1;
    pooled[start] = 1;
    while (!group.empty()) {
      if (stop.stop_requested())
        return std::nullopt;
      const auto current = group.front();
      group.pop();
      const int x = int(current % std::size_t(grid.width));
      const int z = int(current / std::size_t(grid.width));
      for (int neighbour = 0; neighbour < 8; ++neighbour) {
        const int nx = x + NeighbourX[neighbour];
        const int nz = z + NeighbourZ[neighbour];
        if (nx < 0 || nz < 0 || nx >= grid.width || nz >= grid.depth)
          continue;
        const auto target = grid.at(nx, nz);
        if (closed[target])
          continue;
        closed[target] = 1;
        pooled[target] = 1;
        parent[target] = current;
        group.push(target);
      }
    }
  }

  // A priority flood seeded from the border has no interior sink in its tree:
  // every sample ends up with a parent, and every chain of parents runs
  // downhill to the edge. A closed basin is therefore a property of the FILLED
  // surface, not of the tree. A sample the flood had to raise above its own
  // height is standing water, and a connected group of those samples is one
  // depression.
  //
  // The test is deliberately a tolerance and not an epsilon. A sample that
  // merely sits below the level that discovered it is raised by exactly one
  // epsilon, which is the boundary case and not a depression; only a sample
  // filled from a discoverer standing genuinely above it has to be lifted, and
  // that is the depression. The tolerance rejects the epsilon-scale noise and
  // keeps anything a lake could actually occupy.
  float lowest = heights[0], highest = heights[0];
  for (std::size_t i = 1; i < grid.count; ++i) {
    if ((i & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    lowest = std::min(lowest, heights[i]);
    highest = std::max(highest, heights[i]);
  }
  const float tolerance = std::max(16.F * FloodEpsilon,
                                   0.001F * (highest - lowest));
  for (std::size_t i = 0; i < grid.count; ++i) {
    if ((i & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    if (filled[i] - heights[i] > tolerance)
      pooled[i] = 1;
  }

  // Basin ids are handed out by scanning row-major and numbering each new pool
  // in the order it is first met, so ids depend on the surface alone and not on
  // the order the flood happened to pop cells in.
  TerrainDrainage drainage;
  drainage.seaLevel = settings.seaLevel;
  drainage.basin.resize(grid.count);
  std::vector<std::size_t> basinOf(grid.count, 0);
  std::vector<Vec3> outlets;
  {
    // Id 0 is the field itself, leaving by its lowest border sample.
    std::size_t edge = 0;
    for (int z = 0; z < grid.depth; ++z) {
      if (stop.stop_requested())
        return std::nullopt;
      for (int x = 0; x < grid.width; ++x) {
        if (!isBorder(grid, x, z))
          continue;
        const auto cell = grid.at(x, z);
        if (heights[cell] < heights[edge])
          edge = cell;
      }
    }
    const auto position =
        field.position(int(edge % std::size_t(grid.width)),
                       int(edge / std::size_t(grid.width)));
    outlets.push_back({position.x, heights[edge], position.y});
  }
  // The pool doubles as the traversal queue, so a depression is walked once.
  std::vector<std::size_t> pool;
  for (int z = 0; z < grid.depth; ++z)
    for (int x = 0; x < grid.width; ++x) {
      if ((x & 255) == 0 && stop.stop_requested())
        return std::nullopt;
      const auto cell = grid.at(x, z);
      if (!pooled[cell])
        continue;
      auto lowestInPool = cell;
      pool.clear();
      pool.push_back(cell);
      pooled[cell] = 0;
      for (std::size_t head = 0; head < pool.size(); ++head) {
        if ((head & 255) == 0 && stop.stop_requested())
          return std::nullopt;
        const auto current = pool[head];
        if (heights[current] < heights[lowestInPool])
          lowestInPool = current;
        const int cx = int(current % std::size_t(grid.width));
        const int cz = int(current / std::size_t(grid.width));
        for (int neighbour = 0; neighbour < 8; ++neighbour) {
          const int nx = cx + NeighbourX[neighbour];
          const int nz = cz + NeighbourZ[neighbour];
          if (nx < 0 || nz < 0 || nx >= grid.width || nz >= grid.depth)
            continue;
          const auto target = grid.at(nx, nz);
          if (!pooled[target])
            continue;
          pooled[target] = 0;
          pool.push_back(target);
        }
      }
      const auto id = outlets.size();
      const auto position =
          field.position(int(lowestInPool % std::size_t(grid.width)),
                         int(lowestInPool / std::size_t(grid.width)));
      outlets.push_back({position.x, heights[lowestInPool], position.y});
      for (const auto member : pool)
        basinOf[member] = id;
    }
  for (std::size_t i = 0; i < grid.count; ++i) {
    if ((i & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    drainage.basin.set(i, basinOf[i]);
  }
  // Closed basins only: id 0 is the field, so ids run 1..basinCount and the
  // outlet vector holds those plus the field's own at index 0.
  drainage.basinCount = outlets.empty() ? 0 : outlets.size() - 1;
  drainage.basinOutlets = std::move(outlets);

  // Normalised against the interior maximum. A border sample is the field's own
  // outlet and has no upstream area inside the field to report, so it is zero
  // rather than a value that would dominate the normalisation.
  double peak = 0;
  for (int z = 0; z < grid.depth; ++z) {
    if (stop.stop_requested())
      return std::nullopt;
    for (int x = 0; x < grid.width; ++x) {
      if (isBorder(grid, x, z))
        continue;
      peak = std::max(peak, accumulation[grid.at(x, z)]);
    }
  }
  if (peak <= 0)
    peak = 1;
  drainage.flow.resize(grid.count);
  drainage.downstream.resize(grid.count);
  for (int z = 0; z < grid.depth; ++z) {
    if (stop.stop_requested())
      return std::nullopt;
    for (int x = 0; x < grid.width; ++x) {
      const auto cell = grid.at(x, z);
      const auto accumulated = isBorder(grid, x, z) ? 0.0 : accumulation[cell];
      drainage.flow.set(cell, float(std::clamp(accumulated / peak, 0.0, 1.0)));
      drainage.downstream.set(cell, parent[cell]);
    }
  }

  const auto distances = waterDistances(heights, grid, field.size,
                                        settings.seaLevel, stop);
  if (stop.stop_requested())
    return std::nullopt;
  drainage.waterDistance.resize(grid.count);
  for (std::size_t i = 0; i < grid.count; ++i) {
    if ((i & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    drainage.waterDistance.set(i, distances[i]);
  }
  if (stop.stop_requested())
    return std::nullopt;
  return drainage;
}

} // namespace demi::runtime
