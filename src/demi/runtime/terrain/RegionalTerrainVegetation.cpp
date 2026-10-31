#include "demi/runtime/terrain/RegionalTerrainVegetation.h"
#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainScatterConstraints.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace demi::runtime {
namespace {
TerrainScatterResult generateCell(const RegionalTerrainSampler &sampler,
                                  const TerrainVegetationCellKey &key,
                                  std::stop_token stop) {
  const auto &config = sampler.config();
  const bool canopy = key.layer == 1;
  const auto samples = canopy ? config.streaming.vegetationCanopyCellSamples
                              : config.streaming.vegetationCellSamples;
  const auto firstX = key.x * samples;
  const auto firstZ = key.z * samples;
  const auto width = std::int64_t(
      std::llround(config.recipe.size.x / config.baseSampleSpacing));
  const auto depth = std::int64_t(
      std::llround(config.recipe.size.y / config.baseSampleSpacing));
  const auto columns =
      std::uint32_t(std::min<std::int64_t>(samples, width - firstX));
  const auto rows =
      std::uint32_t(std::min<std::int64_t>(samples, depth - firstZ));
  auto tile = sampler.sampleTile({firstX, firstZ, columns, rows, 1}, stop);
  TerrainMasks masks;
  masks.slope.resize(tile.field.heights.size());
  std::vector<std::uint8_t> wet(tile.field.heights.size());
  const auto sea = TerrainRuleContextBuilder::seaLevel(config.recipe);
  for (std::size_t i = 0; i < wet.size(); ++i) {
    masks.slope.set(
        i, std::acos(std::clamp(tile.field.normals.at(i).y, -1.F, 1.F)) *
               180.F / 3.14159265358979323846F);
    wet[i] = tile.field.heights.at(i) <= sea;
  }
  // Absent moisture/distance measurements remain absent. The constructor
  // rejects palettes that require them rather than inventing a local watershed.
  const auto constraints = TerrainScatterConstraints::build(
      tile.field, config.recipe, &masks, {.seaLevel = sea, .authored = true});
  TerrainScatterSettings settings;
  settings.domain = TerrainScatterDomain{
      {config.originX, config.originZ, config.originX + config.recipe.size.x,
       config.originZ + config.recipe.size.y},
      {tile.originX, tile.originZ},
      {tile.originX, tile.originZ, tile.originX + tile.field.size.x,
       tile.originZ + tile.field.size.y}};
  auto palette = *config.palette;
  if (config.streaming.vegetationCanopyRadius > 0) {
    std::erase_if(palette.placements, [canopy](const auto &rule) {
      const auto category = rule.second.category;
      const bool large = category == TerrainScatterCategory::Tree ||
                         category == TerrainScatterCategory::Rock;
      return large != canopy;
    });
  }
  auto result = scatterTerrain(tile.field, config.recipe, palette, settings,
                               &constraints, wet, stop);
  if (result.truncated) {
    throw std::runtime_error(
        "Regional vegetation cell exceeded its placement budget");
  }
  return result;
}
} // namespace

RegionalTerrainVegetation::RegionalTerrainVegetation(
    JobSystem &jobs, std::shared_ptr<const RegionalTerrainSampler> sampler,
    std::string owner)
    : sampler_(std::move(sampler)),
      cells_(
          jobs,
          {.maximumBytes = sampler_->config().streaming.maximumResidentBytes,
           .maximumPendingJobs =
               sampler_->config().streaming.maximumPendingJobs,
           .maximumStartsPerUpdate = 1,
           .maximumPublicationsPerUpdate = 1},
          [sampler = sampler_](const auto &key, std::stop_token stop) {
            return generateCell(*sampler, key, stop);
          },
          std::move(owner), sampler_->config().recipe.paletteId,
          sampler_->config().projectDirectory) {
  const auto &config = sampler_->config();
  if (!config.palette || config.streaming.vegetationCellSamples == 0 ||
      !std::isfinite(config.streaming.vegetationRadius) ||
      config.streaming.vegetationRadius < 0) {
    throw std::invalid_argument(
        "Regional vegetation requires a palette, positive cell samples and a "
        "nonnegative finite radius");
  }
  const auto validateDemand = [&](std::uint32_t samples, double radius) {
    if (samples == 0 || samples > 1024 || !std::isfinite(radius) ||
        radius < 0) {
      throw std::invalid_argument(
          "Invalid regional vegetation layer sampling or radius");
    }
    const double diameterCells =
        2 * radius / (samples * config.baseSampleSpacing) + 2;
    if (diameterCells * diameterCells > 4096) {
      throw std::invalid_argument("Regional vegetation demand exceeds 4096 "
                                  "cells; increase cell size or reduce radius");
    }
  };
  validateDemand(config.streaming.vegetationCellSamples,
                 config.streaming.vegetationRadius);
  validateDemand(config.streaming.vegetationCanopyCellSamples,
                 config.streaming.vegetationCanopyRadius);
  for (double size : {config.recipe.size.x, config.recipe.size.y}) {
    const auto samples = size / config.baseSampleSpacing;
    if (std::abs(samples - std::round(samples)) > 1e-6) {
      throw std::invalid_argument("Regional vegetation requires bounds aligned "
                                  "to the base sample spacing");
    }
  }
  for (const auto &[id, entry] : config.palette->placements) {
    if (entry.moisture.minimum != 0 || entry.moisture.maximum != 1 ||
        entry.waterDistance.minimum != 0 ||
        entry.waterDistance.maximum != std::numeric_limits<float>::max()) {
      throw std::invalid_argument(
          "Regional palette rule " + id +
          " requires moisture or water-distance measurements not supplied by "
          "this regional graph");
    }
  }
}

bool RegionalTerrainVegetation::update(World &world, Vec3 camera,
                                       RuntimePrefabService *prefabs,
                                       std::string &error, bool includeCanopy) {
  const auto &config = sampler_->config();
  std::vector<std::pair<double, TerrainVegetationWorld::Demand>> prioritized;
  const unsigned layers =
      includeCanopy && config.streaming.vegetationCanopyRadius > 0 ? 2 : 1;
  for (unsigned layer = 0; layer < layers; ++layer) {
    const auto samples = layer == 0
                             ? config.streaming.vegetationCellSamples
                             : config.streaming.vegetationCanopyCellSamples;
    const double size = samples * config.baseSampleSpacing;
    const double radius = layer == 0 ? config.streaming.vegetationRadius
                                     : config.streaming.vegetationCanopyRadius;
    const auto lastX = std::int64_t(std::ceil(config.recipe.size.x / size)) - 1;
    const auto lastZ = std::int64_t(std::ceil(config.recipe.size.y / size)) - 1;
    const auto lowX = std::max<std::int64_t>(
        0, std::floor((camera.x - radius - config.originX) / size));
    const auto lowZ = std::max<std::int64_t>(
        0, std::floor((camera.z - radius - config.originZ) / size));
    const auto highX = std::min<std::int64_t>(
        lastX, std::floor((camera.x + radius - config.originX) / size));
    const auto highZ = std::min<std::int64_t>(
        lastZ, std::floor((camera.z + radius - config.originZ) / size));
    for (auto z = lowZ; z <= highZ; ++z) {
      for (auto x = lowX; x <= highX; ++x) {
        const double originX = config.originX + x * size;
        const double originZ = config.originZ + z * size;
        const double nearestX = std::clamp(
            double(camera.x), originX,
            std::min(originX + size, config.originX + config.recipe.size.x));
        const double nearestZ = std::clamp(
            double(camera.z), originZ,
            std::min(originZ + size, config.originZ + config.recipe.size.y));
        const double ground = sampler_->sampleHeight(nearestX, nearestZ);
        const double distance =
            std::hypot(std::hypot(camera.x - nearestX, camera.z - nearestZ),
                       camera.y - ground);
        if (distance <= radius && radius > 0) {
          prioritized.push_back(
              {distance,
               {{{x, z, 0, TerrainVegetationStorage::Procedural,
                  static_cast<std::uint8_t>(layer)},
                 8U * 1024U * 1024U},
                {float(originX), 0, float(originZ)}}});
        }
      }
    }
  }
  std::stable_sort(
      prioritized.begin(), prioritized.end(),
      [](const auto &a, const auto &b) { return a.first < b.first; });
  std::vector<TerrainVegetationWorld::Demand> demand;
  for (const auto &[distance, cell] : prioritized) {
    demand.push_back(cell);
  }
  return cells_.update(world, demand, prefabs, error);
}

bool RegionalTerrainVegetation::clear(World &world,
                                      RuntimePrefabService *prefabs,
                                      std::string &error) {
  return cells_.clear(world, prefabs, error);
}
void RegionalTerrainVegetation::drain() { cells_.drain(); }
bool RegionalTerrainVegetation::hasPendingWork() const {
  return cells_.hasPendingWork();
}

TerrainVegetationResidency::Statistics
RegionalTerrainVegetation::statistics() const {
  return cells_.statistics();
}
} // namespace demi::runtime
