#include "demi/runtime/terrain/TerrainEvaluation.h"
#include <FastNoiseLite.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string_view>

namespace demi::runtime {
namespace {
float checkedHeight(double height) {
  if (!std::isfinite(height) ||
      std::abs(height) > std::numeric_limits<float>::max())
    throw std::invalid_argument(
        "Terrain evaluation produced a nonfinite height");
  return float(height);
}

std::string_view editLayer(const TerrainEdit &edit) {
  if (!edit.layer.empty())
    return edit.layer;
  return edit.kind == TerrainEditKind::Protect ? "protection" : "sculpt";
}
} // namespace

struct TerrainEvaluation::Impl {
  struct Biome {
    const TerrainBiome *settings;
    FastNoiseLite noise;
  };

  bool generationEnabled = false;
  std::size_t defaultBiome = 0;
  std::vector<Biome> biomes;
  std::vector<const TerrainRegion *> regions;
  std::vector<std::size_t> regionBiomes;
  std::vector<const TerrainEdit *> edits;
  std::vector<const TerrainExclusion *> exclusions;
};

TerrainEvaluation::TerrainEvaluation(const TerrainRecipe &recipe)
    : impl_(std::make_unique<Impl>()) {
  recipe.validate();
  std::map<std::string, std::size_t> biomeIndices;
  for (const auto &[id, settings] : recipe.biomes) {
    biomeIndices.emplace(id, impl_->biomes.size());
    FastNoiseLite noise(recipe.seed);
    noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    noise.SetFrequency(1.F / settings.featureSize);
    noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    noise.SetFractalOctaves(settings.octaves);
    noise.SetFractalGain(settings.roughness);
    impl_->biomes.push_back({&settings, noise});
  }
  impl_->defaultBiome = biomeIndices.at(recipe.defaultBiome);

  // Base stages are evaluated before surface edits, regardless of where their
  // layer entries appear. Within a stage, layers and strokes retain their
  // order.
  for (const auto &layer : recipe.layers) {
    if (!layer.enabled)
      continue;
    switch (layer.kind) {
    case TerrainLayerKind::Generation:
      impl_->generationEnabled = true;
      break;
    case TerrainLayerKind::Biome:
      for (const auto &region : recipe.regions) {
        if (region.layer == layer.id) {
          impl_->regions.push_back(&region);
          impl_->regionBiomes.push_back(biomeIndices.at(region.biome));
        }
      }
      break;
    case TerrainLayerKind::Sculpt:
    case TerrainLayerKind::Protection:
      for (const auto &edit : recipe.edits) {
        if (editLayer(edit) == layer.id)
          impl_->edits.push_back(&edit);
      }
      break;
    case TerrainLayerKind::Exclusion:
      for (const auto &exclusion : recipe.exclusions) {
        if (exclusion.layer == layer.id)
          impl_->exclusions.push_back(&exclusion);
      }
      break;
    }
  }
}

TerrainEvaluation::~TerrainEvaluation() = default;
TerrainEvaluation::TerrainEvaluation(TerrainEvaluation &&) noexcept = default;
TerrainEvaluation &
TerrainEvaluation::operator=(TerrainEvaluation &&) noexcept = default;

TerrainBaseSample TerrainEvaluation::baseSample(Vec2 position) const {
  // Most recipes fit on the stack. Larger biome palettes remain unrestricted;
  // scratch is local so one evaluator can serve concurrent sample queries.
  std::array<double, 16> localWeights{};
  std::vector<double> extendedWeights;
  std::span<double> weights;
  if (impl_->biomes.size() <= localWeights.size()) {
    weights = std::span(localWeights).first(impl_->biomes.size());
  } else {
    extendedWeights.resize(impl_->biomes.size());
    weights = extendedWeights;
  }
  weights[impl_->defaultBiome] = 1;
  for (std::size_t index = 0; index < impl_->regions.size(); ++index) {
    const auto &stroke = *impl_->regions[index];
    const double alpha =
        terrainBrushWeight(position, stroke.center, stroke.radius,
                           stroke.strength, stroke.falloff);
    for (auto &weight : weights)
      weight *= 1 - alpha;
    weights[impl_->regionBiomes[index]] += alpha;
  }

  double height = 0;
  if (impl_->generationEnabled) {
    for (std::size_t index = 0; index < impl_->biomes.size(); ++index) {
      if (weights[index] == 0)
        continue;
      const auto &biome = impl_->biomes[index];
      height +=
          weights[index] * (double(biome.settings->baseHeight) +
                            double(biome.settings->heightVariation) *
                                biome.noise.GetNoise(position.x, position.y));
    }
  }
  const auto dominant = std::max_element(weights.begin(), weights.end());
  return {checkedHeight(height), std::size_t(dominant - weights.begin())};
}

float TerrainEvaluation::exclusion(Vec2 position) const {
  double value = 0;
  for (const auto *stroke : impl_->exclusions) {
    const double weight =
        terrainBrushWeight(position, stroke->center, stroke->radius,
                           stroke->strength, stroke->falloff);
    value = value * (1 - weight) + double(stroke->value) * weight;
  }
  return float(std::clamp(value, 0.0, 1.0));
}

const std::vector<const TerrainEdit *> &TerrainEvaluation::edits() const {
  return impl_->edits;
}

const std::vector<const TerrainRegion *> &TerrainEvaluation::regions() const {
  return impl_->regions;
}

const std::vector<const TerrainExclusion *> &
TerrainEvaluation::exclusions() const {
  return impl_->exclusions;
}

bool TerrainEvaluation::generationEnabled() const {
  return impl_->generationEnabled;
}

float applyTerrainEdit(const TerrainEdit &edit, Vec2 position, float previous,
                       const std::function<float(int, int)> &sample, int x,
                       int z, int cellsX, int cellsZ) {
  const double weight = terrainBrushWeight(position, edit.center, edit.radius,
                                           edit.strength, edit.falloff);
  if (weight == 0)
    return previous;

  double height = previous;
  switch (edit.kind) {
  case TerrainEditKind::Raise:
    height += weight * edit.amount;
    break;
  case TerrainEditKind::Lower:
    height -= weight * edit.amount;
    break;
  case TerrainEditKind::Flatten:
    height =
        double(previous) * (1 - weight) + double(edit.targetHeight) * weight;
    break;
  case TerrainEditKind::Smooth: {
    double sum = 0;
    int count = 0;
    for (int dz = -1; dz <= 1; ++dz) {
      for (int dx = -1; dx <= 1; ++dx) {
        const auto sampleX = std::int64_t(x) + dx;
        const auto sampleZ = std::int64_t(z) + dz;
        if (sampleX >= 0 && sampleZ >= 0 && sampleX <= cellsX &&
            sampleZ <= cellsZ) {
          sum += sample(int(sampleX), int(sampleZ));
          ++count;
        }
      }
    }
    height = double(previous) * (1 - weight) + sum / count * weight;
    break;
  }
  case TerrainEditKind::Protect:
    return previous;
  }
  return checkedHeight(height);
}

} // namespace demi::runtime
