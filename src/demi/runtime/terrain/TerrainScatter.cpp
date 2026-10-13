#include "demi/runtime/terrain/TerrainScatter.h"
#include "demi/runtime/terrain/TerrainScatterConstraints.h"
#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainSeed.h"
#include "demi/runtime/terrain/TerrainSurface.h"
#include "demi/runtime/terrain/TerrainWater.h"
#include "demi/runtime/simulation/DeterministicRandom.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace demi::runtime {
namespace {

// A spatial hash cell is at least as large as the coarsest spacing, so a
// candidate only has to check the 3x3 neighbourhood of buckets around it.
struct BucketKey {
  int x = 0;
  int z = 0;
  bool operator==(const BucketKey &) const noexcept = default;
};

struct BucketHash {
  std::size_t operator()(const BucketKey &key) const noexcept {
    return std::hash<std::int64_t>{}(
        (static_cast<std::int64_t>(key.x) << 32) ^
        (std::uint32_t(key.z)));
  }
};

// Derives a per-cell seed from the world seed and the cell index. Using the
// index rather than a running counter is what makes placement independent of
// traversal order: a cell always draws the same numbers, wherever it is
// visited from.
simulation::DeterministicRandom cellRandom(int worldSeed, std::size_t cell,
                                           std::string_view ruleId,
                                           int sample) {
  const auto channel = deriveTerrainSubSeed(worldSeed,
                                            TerrainSeedChannel::Scatter);
  // Fold the cell, the rule and the sample slot together so two roles, or two
  // candidates in the same cell, never share a stream.
  std::uint64_t state = static_cast<std::uint64_t>(std::uint32_t(channel));
  state ^= static_cast<std::uint64_t>(cell) * 0x9E3779B97F4A7C15ull;
  state ^= terrainPlacementRuleHash(ruleId) * 0xC2B2AE3D27D4EB4Full;
  state ^= static_cast<std::uint64_t>(sample) * 0x165667B19E3779F9ull;
  return simulation::DeterministicRandom(state);
}

bool roleAllowsBiome(const TerrainPaletteEntry &entry,
                     const std::string &biomeName) {
  return entry.biomes.empty() ||
         std::find(entry.biomes.begin(), entry.biomes.end(), biomeName) !=
             entry.biomes.end();
}

} // namespace

bool refreshTerrainScatterPlacements(
    HeightField &field, std::span<const TerrainScatterPlacement> candidates,
    const TerrainWaterAuthoring *water, std::stop_token stop) {
  if (stop.stop_requested())
    return false;
  const auto count = field.heights.size();
  if (field.exclusions.size() != count || field.biomeIndices.size() != count)
    throw std::invalid_argument("Terrain placement grid is incomplete");
  terrain_water_detail::WaterLevelField waterLevels;
  if (water != nullptr && water->authored && !water->bodies.empty()) {
    waterLevels = terrain_water_detail::buildWaterLevelField(
        field.size, field.cellsX, field.cellsZ, field.heights, *water);
    if (waterLevels.samples() != count)
      throw std::invalid_argument("Terrain water coverage does not match grid");
  }
  if (stop.stop_requested())
    return false;
  std::vector<TerrainScatterPlacement> visible;
  visible.reserve(candidates.size());
  for (const auto &candidate : candidates) {
    if (stop.stop_requested())
      return false;
    if (candidate.cell >= count)
      throw std::invalid_argument("Terrain placement cell is outside grid");
    if (field.exclusions[candidate.cell] > 0.F ||
        (!waterLevels.wet.empty() && waterLevels.wet[candidate.cell]))
      continue;
    auto placed = candidate;
    const auto height = sampleTerrainHeight(
        field, {placed.position.x, placed.position.z});
    if (!height)
      continue;
    placed.position.y = *height;
    placed.biome = field.biomeIndices[candidate.cell];
    placed.biomeName = field.biomeIds.at(placed.biome);
    visible.push_back(std::move(placed));
  }
  if (stop.stop_requested())
    return false;
  field.scatterPlacements = std::move(visible);
  return true;
}

std::vector<std::string> scatterableRoles(const HeightField &field,
                                          const TerrainRecipe &recipe,
                                          const TerrainPalette &palette) {
  const float sea = TerrainRuleContextBuilder::seaLevel(recipe);
  std::vector<std::string> roles;
  for (const auto &[name, entry] : palette.placements) {
    if (entry.weight <= 0.F || entry.spacing < 0.F)
      continue;
    const auto &ruleId = name;
    // A rule is scatterable when at least one eligible cell exists for it.
    for (int z = 0;
         z <= recipe.cellsZ && roles.size() < palette.placements.size(); ++z)
      for (int x = 0; x <= recipe.cellsX; ++x) {
        const auto index = field.index(x, z);
        if (field.heights[index] <= sea)
          continue;
        if (field.exclusions[index] > 0.F)
          continue;
        if (!roleAllowsBiome(entry, field.biomeIds[field.biomeIndices[index]]))
          continue;
        roles.push_back(ruleId);
        break;
      }
  }
  std::sort(roles.begin(), roles.end());
  roles.erase(std::unique(roles.begin(), roles.end()), roles.end());
  return roles;
}

TerrainScatterResult scatterTerrain(const HeightField &field,
                                    const TerrainRecipe &recipe,
                                    const TerrainPalette &palette,
                                    const TerrainScatterSettings &settings,
                                    const TerrainScatterConstraints *constraints) {
  TerrainScatterResult result;
  if (settings.density <= 0.F)
    return result;

  const float sea = TerrainRuleContextBuilder::seaLevel(recipe);
  const float stepX = field.size.x / float(recipe.cellsX);
  const float stepZ = field.size.y / float(recipe.cellsZ);
  const auto cellArea = std::max(1e-6F, stepX * stepZ);

  // The bucket size is the coarsest spacing any rule asks for, so one
  // neighbourhood lookup is enough to reject an overlapping candidate.
  float coarsest = 0.F;
  for (const auto &[name, entry] : palette.placements)
    if (entry.weight > 0.F && entry.spacing > 0.F)
      coarsest = std::max(coarsest, entry.spacing);
  coarsest = std::max(coarsest, stepX * 2.F);
  const auto bucketSize = coarsest;

  // Roles are visited in a fixed order (the palette map is key-sorted) so the
  // accepted set never depends on authored array order.
  struct Active {
    const TerrainPaletteEntry *entry;
    std::string ruleId;
    std::string name;
  };
  std::vector<Active> active;
  for (const auto &[name, entry] : palette.placements) {
    if (entry.weight <= 0.F)
      continue;
    const auto &ruleId = name;
    active.push_back({&entry, ruleId, name});
  }
  if (active.empty())
    return result;

  // Per-rule spatial hashes, so one rule's spacing never rejects another's.
  std::map<std::string,
           std::unordered_map<BucketKey, std::vector<Vec3>, BucketHash>>
      occupied;
  for (const auto &entry : active)
    occupied[entry.ruleId];

  float weightTotal = 0.F;
  for (const auto &entry : active)
    weightTotal += entry.entry->weight;
  if (weightTotal <= 0.F)
    return result;

  for (int z = 0; z <= recipe.cellsZ; ++z)
    for (int x = 0; x <= recipe.cellsX; ++x) {
      const auto cell = field.index(x, z);
      const auto height = field.heights[cell];
      // The constraints object is the single answer when one is supplied, so the
      // editor can explain a gap and the solver agrees. Null keeps the inline
      // water and exclusion tests this function always had.
      if (constraints != nullptr) {
        if (constraints->blocked(cell))
          continue;
      } else {
        if (height <= sea)
          continue;
        if (field.exclusions[cell] > 0.F)
          continue;
      }
      const auto biome = field.biomeIndices[cell];
      const auto &biomeName = field.biomeIds[biome];

      for (const auto &candidate : active) {
        const auto &entry = *candidate.entry;
        if (!roleAllowsBiome(entry, biomeName))
          continue;
        // Spacing 0 means continuous cover: a fixed share of the cell area.
        const float perCell =
            entry.spacing > 0.F
                ? cellArea / (entry.spacing * entry.spacing)
                : cellArea * 0.25F;
        float share = perCell * (entry.weight / weightTotal) *
                      settings.density;
        if (share <= 0.F)
          continue;

        auto random = cellRandom(recipe.seed, cell, candidate.ruleId, 0);
        if (random.value() > std::min(1.F, share))
          continue;

        const Vec3 position{field.size.x * float(x) / float(recipe.cellsX),
                            height,
                            field.size.y * float(z) / float(recipe.cellsZ)};
        if (entry.spacing > 0.F) {
          auto &grid = occupied[candidate.ruleId];
          const float spacing = entry.spacing * settings.spacingScale;
          const auto bx = static_cast<int>(std::floor(position.x / bucketSize));
          const auto bz = static_cast<int>(std::floor(position.z / bucketSize));
          bool blocked = false;
          for (int dz = -1; dz <= 1 && !blocked; ++dz)
            for (int dx = -1; dx <= 1 && !blocked; ++dx) {
              const auto found =
                  grid.find(BucketKey{bx + dx, bz + dz});
              if (found == grid.end())
                continue;
              for (const auto &other : found->second) {
                const float reach = std::hypot(other.x - position.x,
                                               other.z - position.z);
                if (reach < spacing) {
                  blocked = true;
                  break;
                }
              }
            }
          if (blocked)
            continue;
          grid[BucketKey{bx, bz}].push_back(position);
        }

        auto placed = cellRandom(recipe.seed, cell, candidate.ruleId, 1);
        TerrainScatterPlacement output;
        output.ruleId = candidate.ruleId;
        output.model = entry.model;
        output.prefab = entry.prefab;
        output.biome = biome;
        output.biomeName = biomeName;
        output.cell = cell;
        output.position = position;
        output.yaw = placed.value() * 6.2831853F;
        output.scale = placed.range(entry.scaleMin, entry.scaleMax);
        output.collision = entry.collision;
        output.lod = entry.lod;
        result.placements.push_back(std::move(output));
        if (result.placements.size() >= settings.maximumPlacements) {
          result.truncated = true;
          return result;
        }
      }
    }
  return result;
}

} // namespace demi::runtime
