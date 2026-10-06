#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainEvaluation.h"
#include "demi/runtime/terrain/TerrainSeed.h"
#include <FastNoiseLite.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <queue>
#include <stdexcept>

namespace demi::runtime {
namespace {

Vec2 positionOf(int x, int z, Vec2 size, int cellsX, int cellsZ) {
  return {size.x * float(x) / float(cellsX), size.y * float(z) / float(cellsZ)};
}

} // namespace

// The chamfer distance transform below approximates true Euclidean distance
// with a 3-4 diagonal step weight. It is symmetric, monotonic and cheap, which
// matters more than exactness for a rule threshold.
constexpr float DiagonalWeight = 0.70710678F;

struct TerrainRuleContextBuilder::MoistureField {
  FastNoiseLite noise;
};

TerrainRuleContextBuilder::TerrainRuleContextBuilder(
    const TerrainRecipe &recipe) {
  const auto count = recipe.sampleCount();
  contexts_.assign(count, TerrainRuleContext{});
  moisture_ = std::make_unique<MoistureField>();
  // Moisture is a single shared field, not per-biome, so it stays a stable
  // climate signal when biomes are added or renamed.
  moisture_->noise.SetSeed(
      deriveTerrainSubSeed(recipe.seed, TerrainSeedChannel::BiomePlacement));
  moisture_->noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
  moisture_->noise.SetFrequency(1.F / std::max(1.F, recipe.size.x * .5F));
}

TerrainRuleContextBuilder::~TerrainRuleContextBuilder() = default;
TerrainRuleContextBuilder::TerrainRuleContextBuilder(
    TerrainRuleContextBuilder &&) noexcept = default;
TerrainRuleContextBuilder &TerrainRuleContextBuilder::operator=(
    TerrainRuleContextBuilder &&) noexcept = default;

float TerrainRuleContextBuilder::seaLevel(const TerrainRecipe &recipe) {
  // A quarter of the authored relief band: low enough that basins fill, high
  // enough that ordinary rolling terrain stays dry. Sea level follows the ground
  // shape, so it reads landforms rather than biome appearance.
  float highest = 0;
  for (const auto &[id, shape] : recipe.landforms)
    highest = std::max(highest, shape.baseHeight + shape.heightVariation);
  return std::max(0.F, highest * .25F);
}

void TerrainRuleContextBuilder::build(const TerrainRecipe &recipe,
                                      const std::vector<float> &baseHeights,
                                      int cellsX, int cellsZ, Vec2 size) {
  const auto count = recipe.sampleCount();
  if (baseHeights.size() != count || contexts_.size() != count)
    throw std::invalid_argument(
        "Terrain rule context does not match the heightfield");
  const float sea = seaLevel(recipe);
  const float stepX = size.x / float(cellsX);
  const float stepZ = size.y / float(cellsZ);

  // Water distance: a multi-source chamfer transform seeded from every sample
  // at or below sea level, so a basin measures to the nearest shoreline.
  std::vector<float> distance(count,
                              std::numeric_limits<float>::max());
  std::queue<std::size_t> frontier;
  const auto index = [&](int x, int z) { return std::size_t(z) * (cellsX + 1) + x; };
  for (int z = 0; z <= cellsZ; ++z)
    for (int x = 0; x <= cellsX; ++x)
      if (baseHeights[index(x, z)] <= sea) {
        distance[index(x, z)] = 0;
        frontier.push(index(x, z));
      }
  if (frontier.empty()) {
    // No water anywhere: every sample is maximally far from it, so a
    // water-distance rule cannot accidentally match a narrow band.
    for (int z = 0; z <= cellsZ; ++z)
      for (int x = 0; x <= cellsX; ++x)
        distance[index(x, z)] =
            std::numeric_limits<float>::max() / 4.F;
  } else {
    const float diagonal =
        std::min(stepX, stepZ) * DiagonalWeight;
    const float straight = std::min(stepX, stepZ);
    while (!frontier.empty()) {
      const auto current = frontier.front();
      frontier.pop();
      const int x = int(current % std::size_t(cellsX + 1));
      const int z = int(current / std::size_t(cellsX + 1));
      for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
          if (!dx && !dz)
            continue;
          const int nx = x + dx, nz = z + dz;
          if (nx < 0 || nz < 0 || nx > cellsX || nz > cellsZ)
            continue;
          const auto neighbour = index(nx, nz);
          const float step = dx && dz ? diagonal : straight;
          if (distance[neighbour] > distance[current] + step) {
            distance[neighbour] = distance[current] + step;
            frontier.push(neighbour);
          }
        }
    }
  }

  for (int z = 0; z <= cellsZ; ++z)
    for (int x = 0; x <= cellsX; ++x) {
      const auto cell = index(x, z);
      auto &context = contexts_[cell];
      context.height = baseHeights[cell];
      context.waterDistance = distance[cell];

      // Slope from the central difference of the base surface. Clamped at the
      // borders, which matches how the normal pass reflects its edges.
      const int left = std::max(0, x - 1), right = std::min(cellsX, x + 1);
      const int near = std::max(0, z - 1), far = std::min(cellsZ, z + 1);
      const float dx = (baseHeights[index(right, z)] -
                        baseHeights[index(left, z)]) /
                       std::max(1e-6F, float(right - left) * stepX);
      const float dz = (baseHeights[index(x, far)] -
                        baseHeights[index(x, near)]) /
                       std::max(1e-6F, float(far - near) * stepZ);
      context.slope = std::atan(std::hypot(dx, dz)) * 180.F / 3.14159265F;

      const Vec2 sample = positionOf(x, z, size, cellsX, cellsZ);
      context.moisture = std::clamp(
          0.5F + 0.5F * moisture_->noise.GetNoise(sample.x, sample.y), 0.F,
          1.F);

      if (context.height <= sea)
        context.substrate = TerrainSubstrate::Wet;
      else if (context.slope >= 38.F)
        context.substrate = TerrainSubstrate::Rock;
      else if (context.waterDistance <= std::max(stepX, stepZ) * 2.F)
        context.substrate = TerrainSubstrate::Sand;
      else
        context.substrate = TerrainSubstrate::Soil;
    }
}

const std::vector<TerrainRuleContext> &
TerrainRuleContextBuilder::contexts() const {
  return contexts_;
}

namespace {
// Blended weight of a rule at a sample, in [0,1]. A rule whose bands reject the
// sample scores 0. A blend width ramps acceptance near an elevation edge, so
// neighbouring biomes cross-fade instead of forming a hard staircase.
float ruleWeight(const TerrainBiomeRule &rule,
                 const TerrainRuleContext &context) {
  if (!rule.substrate.empty() &&
      std::find(rule.substrate.begin(), rule.substrate.end(),
                context.substrate) == rule.substrate.end())
    return 0.F;
  if (!rule.slope.contains(context.slope) ||
      !rule.moisture.contains(context.moisture) ||
      !rule.waterDistance.contains(context.waterDistance))
    return 0.F;
  if (!rule.elevation.contains(context.height))
    return 0.F;
  if (rule.blend <= 0.F)
    return 1.F;
  if (!rule.elevation.enabled)
    return 1.F;
  // Ramp only on the interior side of the band, so the boundary stays where it
  // was authored while the transition is smooth.
  const float span = rule.elevation.maximum - rule.elevation.minimum;
  if (span <= 0.F)
    return 1.F;
  const float into = context.height - rule.elevation.minimum;
  if (into >= rule.blend)
    return 1.F;
  return std::clamp(into / rule.blend, 0.F, 1.F);
}
} // namespace

std::vector<TerrainRuleDecision> assignTerrainBiomes(
    const TerrainRecipe &recipe, const std::vector<TerrainRuleContext> &contexts,
    int cellsX, int cellsZ, Vec2 size) {
  const auto count = recipe.sampleCount();
  if (contexts.size() != count)
    throw std::invalid_argument(
        "Terrain rule context does not match the heightfield");

  std::map<std::string, std::size_t> biomeIndices;
  for (const auto &[id, biome] : recipe.biomes)
    biomeIndices.emplace(id, biomeIndices.size());
  const auto defaultBiome = biomeIndices.at(recipe.defaultBiome);

  // Only rules on an enabled biome layer participate. Ordering is by authored
  // layer order, then stroke order, matching the rest of the evaluator.
  std::vector<const TerrainBiomeRule *> active;
  for (const auto &layer : recipe.layers) {
    if (!layer.enabled || layer.kind != TerrainLayerKind::Biome)
      continue;
    for (const auto &rule : recipe.rules)
      if (rule.layer == layer.id)
        active.push_back(&rule);
  }

  TerrainEvaluation evaluation(recipe);
  std::vector<TerrainRuleDecision> decisions(count);
  for (int z = 0; z <= cellsZ; ++z)
    for (int x = 0; x <= cellsX; ++x) {
      const auto cell = std::size_t(z) * (cellsX + 1) + x;
      const auto &context = contexts[cell];
      const auto position = positionOf(x, z, size, cellsX, cellsZ);

      // Highest priority wins. An equal priority is broken by blend weight, so
      // a stronger transition outranks a weaker one; a full tie keeps the
      // earlier rule, which makes the result independent of iteration order.
      const TerrainBiomeRule *best = nullptr;
      float bestWeight = 0;
      for (const auto *rule : active) {
        const float weight = ruleWeight(*rule, context);
        if (weight <= 0.F)
          continue;
        if (!best || rule->priority > best->priority ||
            (rule->priority == best->priority && weight > bestWeight)) {
          best = rule;
          bestWeight = weight;
        }
      }
      decisions[cell].biome = best ? biomeIndices.at(best->biome) : defaultBiome;
      decisions[cell].ruleId = best ? best->id : std::string{};

      // Painted regions are applied after rules and always win, which keeps
      // manual override authoritative. Regions are evaluated per cell, but the
      // biome index is resolved from a map built once above rather than by
      // rescanning recipe.biomes inside the loop.
      std::size_t dominant = decisions[cell].biome;
      double strongest = 0.0;
      for (const auto *region : evaluation.regions()) {
        const double alpha = terrainBrushWeight(
            position, region->center, region->radius, region->strength,
            region->falloff);
        if (alpha <= strongest)
          continue;
        strongest = alpha;
        // Regions are validated against the recipe, so this lookup always hits.
        dominant = biomeIndices.at(region->biome);
      }
      if (strongest > 0.0) {
        decisions[cell].biome = dominant;
        decisions[cell].overridden = true;
        decisions[cell].ruleId.clear();
      }
    }
  return decisions;
}

} // namespace demi::runtime
