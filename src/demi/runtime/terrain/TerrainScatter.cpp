#include "demi/runtime/terrain/TerrainScatter.h"
#include "demi/runtime/simulation/DeterministicRandom.h"
#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainScatterConstraints.h"
#include "demi/runtime/terrain/TerrainSeed.h"
#include "demi/runtime/terrain/TerrainSurface.h"
#include "demi/runtime/terrain/TerrainWater.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace demi::runtime {
namespace {

simulation::DeterministicRandom cellRandom(const TerrainScatterRuleSeed &seed,
                                           std::uint64_t candidate,
                                           std::uint32_t sample) {
  return simulation::DeterministicRandom(seed.candidate(candidate, sample));
}

bool roleAllowsBiome(const TerrainPaletteEntry &entry,
                     const std::string &biomeName) {
  return entry.biomes.empty() ||
         std::find(entry.biomes.begin(), entry.biomes.end(), biomeName) !=
             entry.biomes.end();
}

Vec3 candidatePosition(const TerrainScatterRuleSeed &seed, std::uint64_t key,
                       int x, int z, float step, float jitter) {
  Vec3 position{float(x) * step, 0, float(z) * step};
  if (jitter > 0.F) {
    // A separate stream leaves selection, priority, yaw and scale independent.
    auto random = cellRandom(seed, key, 4);
    const float radius = 0.5F * step * jitter;
    position.x += random.range(-radius, radius);
    position.z += random.range(-radius, radius);
  }
  return position;
}

bool insideTerrain(Vec3 position, Vec2 size) {
  return position.x >= 0.F && position.z >= 0.F && position.x <= size.x &&
         position.z <= size.y;
}

bool insidePotentialSite(Vec3 position, Vec2 size,
                         const TerrainScatterDomain *domain) {
  if (!domain) {
    return insideTerrain(position, size);
  }
  return position.x >= 0.F && position.z >= 0.F &&
         double(position.x) <
             (domain->worldBounds.maxX - domain->worldBounds.minX) &&
         double(position.z) <
             (domain->worldBounds.maxZ - domain->worldBounds.minZ);
}

void validateDomain(const TerrainScatterDomain &domain,
                    const HeightField &field, bool suppliedConstraints,
                    std::size_t wetSamples) {
  const auto &window = domain.ownership;
  const auto &world = domain.worldBounds;
  const double worldWidth = world.maxX - world.minX;
  const double worldDepth = world.maxZ - world.minZ;
  const double fieldMaxX = domain.fieldOrigin.x + field.size.x;
  const double fieldMaxZ = domain.fieldOrigin.z + field.size.y;
  for (const double value :
       {world.minX, world.minZ, world.maxX, world.maxZ, worldWidth, worldDepth,
        domain.fieldOrigin.x, domain.fieldOrigin.z, fieldMaxX, fieldMaxZ,
        window.minX, window.minZ, window.maxX, window.maxZ}) {
    if (!std::isfinite(value)) {
      throw std::invalid_argument(
          "Terrain scatter domain values must be finite");
    }
  }
  if (worldWidth <= 0 || worldDepth <= 0 || fieldMaxX <= domain.fieldOrigin.x ||
      fieldMaxZ <= domain.fieldOrigin.z || window.minX >= window.maxX ||
      window.minZ >= window.maxZ || window.minX < world.minX ||
      window.minZ < world.minZ || window.maxX > world.maxX ||
      window.maxZ > world.maxZ || window.minX < domain.fieldOrigin.x ||
      window.minZ < domain.fieldOrigin.z || window.maxX > fieldMaxX ||
      window.maxZ > fieldMaxZ) {
    throw std::invalid_argument("Terrain scatter ownership must be nonempty "
                                "and inside world/local field bounds");
  }
  if (!suppliedConstraints || wetSamples != field.heights.size()) {
    throw std::invalid_argument(
        "Global terrain scatter requires explicit matching environment "
        "constraints and wet samples");
  }
}

// Compete against immutable potential sites rather than accepted placements.
// Filtering one site can therefore never refill a chain of distant neighbours.
bool winsSpacing(const TerrainScatterRuleSeed &seed, Vec3 position, int x,
                 int z, float step, float spacing, float jitter, Vec2 size,
                 int maximumX, int maximumZ, std::stop_token stop,
                 std::size_t &spacingChecks, std::size_t maximumSpacingChecks,
                 const TerrainScatterDomain *domain) {
  if (spacing <= 0.F) {
    return true;
  }
  const int reach =
      static_cast<int>(std::min(double(std::max(maximumX, maximumZ)),
                                std::ceil(double(spacing) / step + jitter)));
  const auto candidate = terrainScatterCandidateKey(x, z);
  const float priority = cellRandom(seed, candidate, 2).value();
  for (int otherZ = std::max(0, z - reach);
       otherZ <= z + std::min(maximumZ - z, reach); ++otherZ) {
    if (stop.stop_requested()) {
      return false;
    }
    for (int otherX = std::max(0, x - reach);
         otherX <= x + std::min(maximumX - x, reach); ++otherX) {
      if (spacingChecks >= maximumSpacingChecks) {
        throw std::invalid_argument(
            "Terrain scatter spacing work budget exceeded; increase "
            "candidate_spacing or the caller's maximumSpacingChecks budget.");
      }
      ++spacingChecks;
      const auto other = terrainScatterCandidateKey(otherX, otherZ);
      if (other == candidate) {
        continue;
      }
      // Priority is a pure site stream: a losing neighbor cannot reject this
      // candidate. Count it above, but avoid its jitter and geometry work.
      const float otherPriority = cellRandom(seed, other, 2).value();
      const bool canWin = otherPriority < priority ||
                          (otherPriority == priority && other < candidate);
      if (!canWin) {
        continue;
      }
      const Vec3 otherPosition =
          candidatePosition(seed, other, otherX, otherZ, step, jitter);
      if (!insidePotentialSite(otherPosition, size, domain)) {
        continue;
      }
      const float distance =
          jitter == 0.F
              ? std::hypot(float(otherX - x) * step, float(otherZ - z) * step)
              : std::hypot(otherPosition.x - position.x,
                           otherPosition.z - position.z);
      if (distance >= spacing) {
        continue;
      }
      return false;
    }
  }
  return true;
}

struct ClusterCorners {
  std::uint64_t xBits = 0;
  std::uint64_t zBits = 0;
  float nearLeft = 0;
  float nearRight = 0;
  float farLeft = 0;
  float farRight = 0;
  bool valid = false;
};

float clusterDensity(const TerrainPaletteEntry &entry,
                     const TerrainScatterRuleSeed &seed, double positionX,
                     double positionZ, ClusterCorners &corners) {
  if (entry.clusterStrength <= 0.F) {
    return 1.F;
  }
  const double gridX = positionX / entry.clusterSize;
  const double gridZ = positionZ / entry.clusterSize;
  const double x = std::floor(gridX);
  const double z = std::floor(gridZ);
  const auto value = [&](double sampleX, double sampleZ) {
    const auto key = std::bit_cast<std::uint64_t>(sampleX) * 0x9E3779B9ULL ^
                     std::bit_cast<std::uint64_t>(sampleZ) * 0xC2B2AE35ULL;
    return cellRandom(seed, key, 3).value();
  };
  // Adjacent sites often share a cluster cell. Keep only the last cell for
  // this rule and call; bitwise keys preserve the seed's signed-zero identity.
  const auto xBits = std::bit_cast<std::uint64_t>(x);
  const auto zBits = std::bit_cast<std::uint64_t>(z);
  if (!corners.valid || corners.xBits != xBits || corners.zBits != zBits) {
    corners.xBits = xBits;
    corners.zBits = zBits;
    corners.nearLeft = value(x, z);
    corners.nearRight = value(x + 1, z);
    corners.farLeft = value(x, z + 1);
    corners.farRight = value(x + 1, z + 1);
    corners.valid = true;
  }
  const auto smooth = [](float t) { return t * t * (3.F - 2.F * t); };
  const float blendX = smooth(float(gridX - x));
  const float blendZ = smooth(float(gridZ - z));
  const float near = std::lerp(corners.nearLeft, corners.nearRight, blendX);
  const float far = std::lerp(corners.farLeft, corners.farRight, blendX);
  // The smooth field creates coherent patches without moving stable sites.
  const float clustered =
      std::clamp((std::lerp(near, far, blendZ) - 0.35F) * 3.F, 0.F, 1.F);
  return std::lerp(1.F, clustered, entry.clusterStrength);
}

TerrainScatterConstraints
constraintsForSurface(const HeightField &field, const TerrainRecipe &recipe,
                      std::span<const std::uint8_t> resolvedWetSamples) {
  TerrainRecipe measurementRecipe = recipe;
  measurementRecipe.cellsX = field.cellsX;
  measurementRecipe.cellsZ = field.cellsZ;
  measurementRecipe.size = field.size;
  TerrainRuleContextBuilder builder(measurementRecipe);
  const std::vector<float> heights(field.heights.begin(), field.heights.end());
  const std::vector<std::uint8_t> wet(resolvedWetSamples.begin(),
                                      resolvedWetSamples.end());
  builder.build(measurementRecipe, heights, field.cellsX, field.cellsZ,
                field.size, wet.empty() ? nullptr : &wet);
  TerrainMasks masks;
  for (const auto &context : builder.contexts()) {
    masks.slope.push_back(context.slope);
    masks.moisture.push_back(context.moisture);
    masks.waterDistance.push_back(context.waterDistance);
  }
  TerrainWaterLevel water;
  if (!resolvedWetSamples.empty()) {
    water.authored = true;
    water.seaLevel = std::numeric_limits<float>::lowest();
  }
  return TerrainScatterConstraints::build(field, recipe, &masks, water);
}

} // namespace

bool refreshTerrainScatterPlacements(
    HeightField &field, std::span<const TerrainScatterPlacement> candidates,
    const TerrainRecipe &recipe, const TerrainPalette *palette,
    const TerrainWaterAuthoring *water, std::stop_token stop) {
  if (stop.stop_requested())
    return false;
  const auto count = field.heights.size();
  if (field.exclusions.size() != count || field.biomeIndices.size() != count)
    throw std::invalid_argument("Terrain placement grid is incomplete");
  if (candidates.empty()) {
    field.scatterPlacements.clear();
    return true;
  }
  terrain_water_detail::WaterLevelField waterLevels;
  if (water != nullptr && water->authored) {
    waterLevels = terrain_water_detail::buildWaterLevelField(
        field.size, field.cellsX, field.cellsZ, field.heights, *water, stop);
    if (stop.stop_requested()) {
      return false;
    }
    if (waterLevels.samples() != count)
      throw std::invalid_argument("Terrain water coverage does not match grid");
  }
  if (stop.stop_requested())
    return false;
  const auto constraints =
      constraintsForSurface(field, recipe, waterLevels.wet);
  if (palette == nullptr) {
    palette = field.resolvedPalette.get();
  }
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
    const auto biome = field.biomeIndices[candidate.cell];
    const auto &biomeName = field.biomeIds.at(biome);
    if (palette != nullptr) {
      const auto rule = palette->placements.find(candidate.ruleId);
      if (rule == palette->placements.end()) {
        throw std::invalid_argument(
            "Terrain candidate refers to an unavailable palette rule: " +
            candidate.ruleId);
      }
      if (!roleAllowsBiome(rule->second, biomeName) ||
          !constraints.allows(
              candidate.cell, rule->second,
              Vec2{candidate.position.x, candidate.position.z})) {
        continue;
      }
    } else if (constraints.blocked(candidate.cell)) {
      continue;
    }
    auto placed = candidate;
    const auto height =
        sampleTerrainHeight(field, {placed.position.x, placed.position.z});
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
  std::vector<std::string> roles;
  for (const auto &[name, entry] : palette.placements) {
    TerrainPalette isolated = palette;
    isolated.placements.clear();
    isolated.placements.emplace(name, entry);
    if (!scatterTerrain(field, recipe, isolated, {.maximumPlacements = 1})
             .placements.empty()) {
      roles.push_back(name);
    }
  }
  return roles;
}

TerrainScatterResult scatterTerrain(
    const HeightField &field, const TerrainRecipe &recipe,
    const TerrainPalette &palette, const TerrainScatterSettings &settings,
    const TerrainScatterConstraints *constraints,
    std::span<const std::uint8_t> resolvedWetSamples, std::stop_token stop) {
  if (!resolvedWetSamples.empty() &&
      resolvedWetSamples.size() != field.heights.size()) {
    throw std::invalid_argument(
        "Terrain scatter water coverage does not match the heightfield");
  }
  TerrainScatterResult result;
  if (!std::isfinite(settings.density) ||
      !std::isfinite(settings.spacingScale) || settings.density < 0.F ||
      settings.spacingScale < 0.F) {
    throw std::invalid_argument("Terrain scatter density and spacing scale "
                                "must be finite and nonnegative.");
  }
  if (settings.density == 0.F || stop.stop_requested()) {
    return result;
  }
  if (settings.maximumPlacements == 0) {
    result.truncated = !palette.placements.empty();
    return result;
  }

  if (field.cellsX <= 0 || field.cellsZ <= 0 || !std::isfinite(field.size.x) ||
      !std::isfinite(field.size.y) || field.size.x <= 0 || field.size.y <= 0 ||
      field.heights.size() !=
          std::size_t(field.cellsX + 1) * std::size_t(field.cellsZ + 1) ||
      field.exclusions.size() != field.heights.size() ||
      field.biomeIndices.size() != field.heights.size()) {
    throw std::invalid_argument(
        "Terrain scatter needs a complete finite heightfield grid.");
  }
  const auto *domain = settings.domain ? &*settings.domain : nullptr;
  if (domain) {
    validateDomain(*domain, field, constraints != nullptr,
                   resolvedWetSamples.size());
  }
  std::optional<TerrainScatterConstraints> derivedConstraints;
  if (constraints == nullptr) {
    derivedConstraints =
        constraintsForSurface(field, recipe, resolvedWetSamples);
    constraints = &*derivedConstraints;
  }

  std::size_t testedCandidates = 0;
  std::size_t spacingChecks = 0;
  for (const auto &[ruleId, entry] : palette.placements) {
    const double density = double(entry.density) *
                           palette.densityFor(entry.category) *
                           settings.density;
    if (density <= 0) {
      continue;
    }
    const TerrainScatterRuleSeed seed(recipe.seed,
                                      terrainPlacementRuleHash(ruleId));
    const float step = entry.candidateStep();
    if (!std::isfinite(step) || step <= 0.F || !std::isfinite(entry.spacing) ||
        entry.spacing < 0.F || !std::isfinite(entry.positionJitter) ||
        entry.positionJitter < 0.F || entry.positionJitter > 1.F) {
      throw std::invalid_argument(
          "Terrain scatter candidate_spacing must be positive and finite, "
          "spacing nonnegative, and position_jitter between 0 and 1.");
    }
    if (domain) {
      for (const double anchor :
           {domain->worldBounds.minX, domain->worldBounds.maxX,
            domain->worldBounds.minZ, domain->worldBounds.maxZ}) {
        if (!(anchor + double(step) > anchor) ||
            !std::isfinite(anchor + double(step))) {
          throw std::invalid_argument("Terrain scatter candidate spacing is "
                                      "not representable at global bounds");
        }
      }
    }
    const double probability = density * double(step) * step;
    if (!std::isfinite(probability) || probability > 1.000001) {
      throw std::invalid_argument("Terrain placement " + ruleId +
                                  " density exceeds candidate capacity; reduce "
                                  "candidate_spacing or density.");
    }
    const double latticeWidth = std::floor(
        (domain ? (domain->worldBounds.maxX - domain->worldBounds.minX)
                : double(field.size.x)) /
        step);
    const double latticeDepth = std::floor(
        (domain ? (domain->worldBounds.maxZ - domain->worldBounds.minZ)
                : double(field.size.y)) /
        step);
    if (latticeWidth >= std::numeric_limits<int>::max() ||
        latticeDepth >= std::numeric_limits<int>::max()) {
      throw std::invalid_argument("Terrain scatter candidate_spacing produces "
                                  "an unrepresentable lattice.");
    }
    const int maximumX = static_cast<int>(latticeWidth);
    const int maximumZ = static_cast<int>(latticeDepth);
    int firstX = 0;
    int firstZ = 0;
    int lastX = maximumX;
    int lastZ = maximumZ;
    if (domain) {
      // Include displaced sites whose lattice centres are outside ownership.
      // The margin also covers rounding in the existing float site generator.
      const auto range = [&](double minimum, double maximum, double origin,
                             int limit) {
        const double radius = 0.5 * step * entry.positionJitter;
        const double margin =
            4 * std::numeric_limits<float>::epsilon() *
            (std::max(std::abs(minimum - origin), std::abs(maximum - origin)) +
             step);
        const double low =
            std::floor((minimum - origin - radius - margin) / step);
        const double high =
            std::ceil((maximum - origin + radius + margin) / step);
        return std::pair{int(std::clamp(low, 0.0, double(limit))),
                         int(std::clamp(high, 0.0, double(limit)))};
      };
      const auto xRange = range(domain->ownership.minX, domain->ownership.maxX,
                                domain->worldBounds.minX, maximumX);
      const auto zRange = range(domain->ownership.minZ, domain->ownership.maxZ,
                                domain->worldBounds.minZ, maximumZ);
      firstX = xRange.first;
      lastX = xRange.second;
      firstZ = zRange.first;
      lastZ = zRange.second;
    }
    const double requestedCandidates =
        (double(lastX) - firstX + 1) * (double(lastZ) - firstZ + 1);
    if (requestedCandidates >
        double(settings.maximumCandidates - testedCandidates)) {
      throw std::invalid_argument(
          "Terrain scatter candidate work budget exceeded; increase "
          "candidate_spacing or the caller's maximumCandidates budget.");
    }
    testedCandidates += static_cast<std::size_t>(requestedCandidates);
    ClusterCorners clusterCorners;
    for (int z = firstZ; z <= lastZ; ++z) {
      if (stop.stop_requested()) {
        return {};
      }
      for (int x = firstX; x <= lastX; ++x) {
        if ((x & 1023) == 0 && stop.stop_requested()) {
          return {};
        }
        const auto candidateKey = terrainScatterCandidateKey(x, z);
        Vec3 position = candidatePosition(seed, candidateKey, x, z, step,
                                          entry.positionJitter);
        const Vec3 potentialPosition = position;
        if (!insidePotentialSite(position, field.size, domain)) {
          continue;
        }
        double clusterX = position.x;
        double clusterZ = position.z;
        if (domain) {
          clusterX = domain->worldBounds.minX + position.x;
          clusterZ = domain->worldBounds.minZ + position.z;
          const auto &window = domain->ownership;
          if (clusterX < window.minX || clusterX >= window.maxX ||
              clusterZ < window.minZ || clusterZ >= window.maxZ) {
            continue;
          }
          position.x = float(clusterX - domain->fieldOrigin.x);
          position.z = float(clusterZ - domain->fieldOrigin.z);
        }
        auto random = cellRandom(seed, candidateKey, 0);
        const float selection = random.value();
        const float clusteredProbability =
            float(probability) *
            clusterDensity(entry, seed, clusterX, clusterZ, clusterCorners);
        if (selection >= clusteredProbability) {
          continue;
        }
        const int sampleX = std::clamp(
            int(std::lround(position.x * float(field.cellsX) / field.size.x)),
            0, field.cellsX);
        const int sampleZ = std::clamp(
            int(std::lround(position.z * float(field.cellsZ) / field.size.y)),
            0, field.cellsZ);
        const auto cell = field.index(sampleX, sampleZ);
        if (!resolvedWetSamples.empty() && resolvedWetSamples[cell] != 0) {
          continue;
        }
        const auto biome = field.biomeIndices[cell];
        const auto &biomeName = field.biomeIds[biome];
        if (!roleAllowsBiome(entry, biomeName) ||
            !constraints->allows(cell, entry, Vec2{position.x, position.z})) {
          continue;
        }
        if (!winsSpacing(seed, potentialPosition, x, z, step,
                         entry.spacing * settings.spacingScale,
                         entry.positionJitter, field.size, maximumX, maximumZ,
                         stop, spacingChecks, settings.maximumSpacingChecks,
                         domain)) {
          continue;
        }
        const auto height =
            sampleTerrainHeight(field, {position.x, position.z});
        if (!height) {
          continue;
        }
        position.y = *height;
        auto variation = cellRandom(seed, candidateKey, 1);
        TerrainScatterPlacement output;
        output.ruleId = ruleId;
        output.model = entry.model;
        output.prefab = entry.prefab;
        output.biome = biome;
        output.biomeName = biomeName;
        output.cell = cell;
        output.candidateX = x;
        output.candidateZ = z;
        output.position = position;
        output.yaw = variation.range(entry.yawDegrees.minimum,
                                     entry.yawDegrees.maximum) *
                     (3.14159265358979323846F / 180.F);
        output.scale = variation.range(entry.scaleMin, entry.scaleMax);
        output.collision = entry.collision;
        result.placements.push_back(std::move(output));
        if (result.placements.size() >= settings.maximumPlacements) {
          result.truncated = true;
          return result;
        }
      }
    }
  }
  return result;
}

} // namespace demi::runtime
