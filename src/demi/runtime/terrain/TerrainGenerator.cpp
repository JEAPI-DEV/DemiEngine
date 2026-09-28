#include "demi/runtime/terrain/TerrainGenerator.h"
#include <FastNoiseLite.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace demi::runtime {
std::size_t HeightField::index(int x, int z) const {
  if (x < 0 || z < 0 || x > cellsX || z > cellsZ)
    throw std::out_of_range("Terrain sample outside grid");
  return std::size_t(z) * (std::size_t(cellsX) + 1) + std::size_t(x);
}
Vec2 HeightField::position(int x, int z) const {
  (void)index(x, z);
  return {float(double(x) * size.x / cellsX),
          float(double(z) * size.y / cellsZ)};
}
float HeightField::height(int x, int z) const {
  return heights.at(index(x, z));
}
Vec3 HeightField::normal(int x, int z) const { return normals.at(index(x, z)); }

namespace {
float checkedHeight(double height) {
  if (!std::isfinite(height) ||
      std::abs(height) > std::numeric_limits<float>::max())
    throw std::invalid_argument("Terrain edit produced a nonfinite height");
  return float(height);
}
} // namespace

std::optional<HeightField>
TerrainGenerator::generate(const TerrainRecipe &recipe, std::stop_token stop,
                           const Progress &progress) {
  if (stop.stop_requested())
    return std::nullopt;
  recipe.validate();
  if (stop.stop_requested())
    return std::nullopt;
  HeightField field;
  field.size = recipe.size;
  field.cellsX = recipe.cellsX;
  field.cellsZ = recipe.cellsZ;
  const auto count = recipe.sampleCount();
  const std::size_t rows = std::size_t(recipe.cellsZ) + 1;
  const double total = double(rows) * (2.0 + recipe.edits.size()) + 1;
  double completed = 0;
  auto report = [&] {
    if (progress)
      progress(float(completed / total));
  };
  report();
  if (stop.stop_requested())
    return std::nullopt;
  field.baseHeights.resize(count);
  field.heights.resize(count);
  field.normals.resize(count);
  field.biomeIndices.resize(count);
  std::vector<unsigned char> protectedSamples(count, 0);
  std::vector<FastNoiseLite> noises;
  std::vector<const TerrainBiome *> biomes;
  for (const auto &[id, biome] : recipe.biomes) {
    field.biomeIds.push_back(id);
    field.biomeColors.push_back(biome.color);
    biomes.push_back(&biome);
    FastNoiseLite noise(recipe.seed);
    noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    noise.SetFrequency(1.F / biome.featureSize);
    noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    noise.SetFractalOctaves(biome.octaves);
    noise.SetFractalGain(biome.roughness);
    noises.push_back(noise);
  }
  auto biomeIndex = [&](const std::string &id) {
    return std::size_t(
        std::lower_bound(field.biomeIds.begin(), field.biomeIds.end(), id) -
        field.biomeIds.begin());
  };
  const auto defaultIndex = biomeIndex(recipe.defaultBiome);
  std::vector<std::size_t> regionIndices;
  for (const auto &region : recipe.regions)
    regionIndices.push_back(biomeIndex(region.biome));
  std::vector<double> weights(biomes.size());
  for (int z = 0;; ++z) {
    if (stop.stop_requested())
      return std::nullopt;
    for (int x = 0;; ++x) {
      if ((x & 255) == 0 && stop.stop_requested())
        return std::nullopt;
      const Vec2 position = field.position(x, z);
      std::fill(weights.begin(), weights.end(), 0);
      weights[defaultIndex] = 1;
      for (std::size_t regionIndex = 0; regionIndex < recipe.regions.size();
           ++regionIndex) {
        const auto &region = recipe.regions[regionIndex];
        const double alpha =
            terrainBrushWeight(position, region.center, region.radius,
                               region.strength, region.falloff);
        for (auto &weight : weights)
          weight *= 1 - alpha;
        weights[regionIndices[regionIndex]] += alpha;
      }
      double height = 0;
      for (std::size_t biomeIndex = 0; biomeIndex < biomes.size();
           ++biomeIndex) {
        if (weights[biomeIndex] == 0)
          continue;
        const auto &biome = *biomes[biomeIndex];
        height += weights[biomeIndex] *
                  (double(biome.baseHeight) +
                   double(biome.heightVariation) *
                       noises[biomeIndex].GetNoise(position.x, position.y));
      }
      const auto index = field.index(x, z);
      field.baseHeights[index] = field.heights[index] = checkedHeight(height);
      field.biomeIndices[index] = std::size_t(
          std::max_element(weights.begin(), weights.end()) - weights.begin());
      if (x == recipe.cellsX)
        break;
    }
    ++completed;
    report();
    if (z == recipe.cellsZ)
      break;
  }
  for (const auto &edit : recipe.edits) {
    if (stop.stop_requested())
      return std::nullopt;
    if (edit.kind == TerrainEditKind::Protect) {
      for (const auto &sample : edit.samples) {
        if (stop.stop_requested())
          return std::nullopt;
        const int x = int(std::llround(double(sample.position.x) /
                                       field.size.x * field.cellsX));
        const int z = int(std::llround(double(sample.position.y) /
                                       field.size.y * field.cellsZ));
        const auto sampleIndex = field.index(x, z);
        // A later explicit protection snapshot replaces an earlier one.
        field.heights[sampleIndex] = sample.height;
        protectedSamples[sampleIndex] = 1;
      }
      completed += rows;
      report();
      continue;
    }
    // Jacobi-style smoothing: every stencil reads the same pre-edit field.
    const auto snapshot = edit.kind == TerrainEditKind::Smooth
                              ? field.heights
                              : std::vector<float>{};
    for (int z = 0;; ++z) {
      if (stop.stop_requested())
        return std::nullopt;
      for (int x = 0;; ++x) {
        if ((x & 255) == 0 && stop.stop_requested())
          return std::nullopt;
        const auto sampleIndex = field.index(x, z);
        const double weight =
            terrainBrushWeight(field.position(x, z), edit.center, edit.radius,
                               edit.strength, edit.falloff);
        if (weight > 0 && !protectedSamples[sampleIndex]) {
          const double previousHeight = field.heights[sampleIndex];
          double height = previousHeight;
          switch (edit.kind) {
          case TerrainEditKind::Raise:
            height += weight * edit.amount;
            break;
          case TerrainEditKind::Lower:
            height -= weight * edit.amount;
            break;
          case TerrainEditKind::Flatten:
            height = previousHeight * (1 - weight) +
                     double(edit.targetHeight) * weight;
            break;
          case TerrainEditKind::Smooth: {
            double sum = 0;
            int samples = 0;
            for (int dz = -1; dz <= 1; ++dz)
              for (int dx = -1; dx <= 1; ++dx) {
                const auto sampleX = std::int64_t(x) + dx;
                const auto sampleZ = std::int64_t(z) + dz;
                if (sampleX >= 0 && sampleZ >= 0 && sampleX <= field.cellsX &&
                    sampleZ <= field.cellsZ) {
                  sum += snapshot[field.index(int(sampleX), int(sampleZ))];
                  ++samples;
                }
              }
            height = previousHeight * (1 - weight) + sum / samples * weight;
            break;
          }
          case TerrainEditKind::Protect:
            break;
          }
          field.heights[sampleIndex] = checkedHeight(height);
        }
        if (x == field.cellsX)
          break;
      }
      ++completed;
      report();
      if (z == field.cellsZ)
        break;
    }
  }
  for (int z = 0;; ++z) {
    if (stop.stop_requested())
      return std::nullopt;
    for (int x = 0;; ++x) {
      if ((x & 255) == 0 && stop.stop_requested())
        return std::nullopt;
      const int left = x > 0 ? x - 1 : x;
      const int right = x < field.cellsX ? x + 1 : x;
      const int down = z > 0 ? z - 1 : z;
      const int up = z < field.cellsZ ? z + 1 : z;
      const double dx =
          (double(field.height(right, z)) - field.height(left, z)) /
          (double(right - left) * field.size.x / field.cellsX);
      const double dz = (double(field.height(x, up)) - field.height(x, down)) /
                        (double(up - down) * field.size.y / field.cellsZ);
      const double length = std::hypot(dx, 1.0, dz);
      field.normals[field.index(x, z)] = {
          float(-dx / length), float(1 / length), float(-dz / length)};
      if (x == field.cellsX)
        break;
    }
    ++completed;
    report();
    if (z == field.cellsZ)
      break;
  }
  for (int z = 0; z < field.cellsZ;) {
    if (stop.stop_requested())
      return std::nullopt;
    const int depth = std::min(recipe.chunkCells, field.cellsZ - z);
    for (int x = 0; x < field.cellsX;) {
      if (stop.stop_requested())
        return std::nullopt;
      const int width = std::min(recipe.chunkCells, field.cellsX - x);
      field.chunks.push_back({x, z, width, depth});
      x += width;
    }
    z += depth;
  }
  ++completed;
  report();
  if (stop.stop_requested())
    return std::nullopt;
  return field;
}

TerrainEdit createProtectionEdit(const HeightField &field, Vec2 center,
                                 float radius, float strength, float falloff) {
  TerrainEdit edit;
  edit.kind = TerrainEditKind::Protect;
  edit.center = center;
  edit.radius = radius;
  edit.strength = strength;
  edit.falloff = falloff;
  edit.snapshotSize = field.size;
  edit.snapshotCellsX = field.cellsX;
  edit.snapshotCellsZ = field.cellsZ;
  TerrainRecipe validation;
  validation.size = field.size;
  validation.cellsX = field.cellsX;
  validation.cellsZ = field.cellsZ;
  validation.biomes.at("default").heightVariation = 0;
  validation.biomes.at("default").octaves = 1;
  validation.biomes.at("default").featureSize =
      std::max(field.size.x, field.size.y);
  validation.edits.push_back(edit);
  validation.validate();
  if (field.heights.size() != validation.sampleCount())
    throw std::invalid_argument(
        "Protection source height array does not match grid");
  for (int z = 0;; ++z) {
    for (int x = 0;; ++x) {
      const auto position = field.position(x, z);
      if (terrainBrushWeight(position, center, radius, strength, falloff) > 0)
        edit.samples.push_back({position, field.height(x, z)});
      if (x == field.cellsX)
        break;
    }
    if (z == field.cellsZ)
      break;
  }
  validation.edits.back() = edit;
  validation.validate();
  return edit;
}
} // namespace demi::runtime
