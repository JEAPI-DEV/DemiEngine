#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainBrushBounds.h"
#include "demi/runtime/terrain/TerrainEvaluation.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainScatter.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

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

std::optional<HeightField>
TerrainGenerator::generateBase(const TerrainRecipe &recipe,
                               std::stop_token stop, const Progress &progress) {
  if (stop.stop_requested())
    return std::nullopt;
  const TerrainEvaluation evaluation(recipe);
  if (stop.stop_requested())
    return std::nullopt;
  HeightField field;
  field.size = recipe.size;
  field.cellsX = recipe.cellsX;
  field.cellsZ = recipe.cellsZ;
  const auto count = recipe.sampleCount();
  const std::size_t rows = std::size_t(recipe.cellsZ) + 1;
  const double total = double(rows) + 1;
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
  field.biomeIndices.resize(count);
  for (const auto &[id, biome] : recipe.biomes) {
    field.biomeIds.push_back(id);
    field.biomeColors.push_back(biome.color);
    field.biomeMaterials.push_back(biome.material);
    field.biomeTextureScales.push_back(biome.textureScale);
  }
  for (int z = 0;; ++z) {
    if (stop.stop_requested())
      return std::nullopt;
    for (int x = 0;; ++x) {
      if ((x & 255) == 0 && stop.stop_requested())
        return std::nullopt;
      const Vec2 position = field.position(x, z);
      const auto sample = evaluation.baseSample(position);
      const auto index = field.index(x, z);
      field.baseHeights.set(index, sample.height);
      field.heights.set(index, sample.height);
      field.biomeIndices.set(index, sample.biome);
      if (x == recipe.cellsX)
        break;
    }
    ++completed;
    report();
    if (z == recipe.cellsZ)
      break;
  }
  // Biome rules read the finished base surface, so they run as a field pass
  // between generation and sculpting. Without rules the field keeps the base
  // pass's per-point assignment, which is the pre-milestone-2 behaviour.
  if (!recipe.rules.empty()) {
    std::vector<float> baseHeights(count);
    for (int z = 0; z <= recipe.cellsZ; ++z) {
      if (stop.stop_requested())
        return std::nullopt;
      for (int x = 0; x <= recipe.cellsX; ++x)
        baseHeights[std::size_t(z) * (recipe.cellsX + 1) + x] =
            field.baseHeights[field.index(x, z)];
    }
    if (stop.stop_requested())
      return std::nullopt;
    TerrainRuleContextBuilder contexts(recipe);
    contexts.build(recipe, baseHeights, recipe.cellsX, recipe.cellsZ,
                   field.size);
    if (stop.stop_requested())
      return std::nullopt;
    const auto decisions = assignTerrainBiomes(
        recipe, contexts.contexts(), recipe.cellsX, recipe.cellsZ, field.size);
    if (stop.stop_requested())
      return std::nullopt;
    for (int z = 0; z <= recipe.cellsZ; ++z)
      for (int x = 0; x <= recipe.cellsX; ++x) {
        if (stop.stop_requested())
          return std::nullopt;
        field.biomeIndices.set(
            field.index(x, z),
            decisions[std::size_t(z) * (recipe.cellsX + 1) + x].biome);
      }
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

bool TerrainGenerator::applyTerrainSurfaceLayers(HeightField &field,
                                                 const TerrainRecipe &recipe,
                                                 std::stop_token stop,
                                                 const Progress &progress) {
  if (stop.stop_requested())
    return false;
  if (field.cellsX != recipe.cellsX || field.cellsZ != recipe.cellsZ ||
      field.size.x != recipe.size.x || field.size.y != recipe.size.y ||
      field.heights.size() != recipe.sampleCount())
    throw std::invalid_argument(
        "Terrain surface layers do not match the recipe");
  const TerrainEvaluation evaluation(recipe);
  const auto count = field.heights.size();
  const auto rows = std::size_t(field.cellsZ) + 1;
  const double total = double(rows) * (2.0 + evaluation.edits().size());
  double completed = 0;
  const auto report = [&] {
    if (progress)
      progress(float(completed / total));
  };
  report();
  field.exclusions.resize(count);
  std::vector<unsigned char> protectedSamples(count, 0);
  for (int z = 0; z <= field.cellsZ; ++z) {
    if (stop.stop_requested())
      return false;
    for (int x = 0; x <= field.cellsX; ++x) {
      if ((x & 255) == 0 && stop.stop_requested())
        return false;
      const auto index = field.index(x, z);
      field.exclusions.set(index, evaluation.exclusion(field.position(x, z)));
    }
    ++completed;
    report();
  }
  for (const auto *activeEdit : evaluation.edits()) {
    const auto &edit = *activeEdit;
    if (stop.stop_requested())
      return false;
    if (edit.kind == TerrainEditKind::Protect) {
      for (const auto &sample : edit.samples) {
        if (stop.stop_requested())
          return false;
        const int x = int(std::llround(double(sample.position.x) /
                                       field.size.x * field.cellsX));
        const int z = int(std::llround(double(sample.position.y) /
                                       field.size.y * field.cellsZ));
        const auto sampleIndex = field.index(x, z);
        // A later explicit protection snapshot replaces an earlier one.
        field.heights.set(sampleIndex, sample.height);
        protectedSamples[sampleIndex] = 1;
      }
      completed += rows;
      report();
      continue;
    }
    const auto affected =
        terrainBrushSampleBounds(field, edit.center, edit.radius);
    if (affected.empty() || edit.strength == 0) {
      completed += rows;
      report();
      continue;
    }
    completed += rows - std::size_t(affected.maxZ - affected.minZ + 1);
    // Jacobi-style smoothing: every stencil reads the same pre-edit field.
    const auto snapshot = edit.kind == TerrainEditKind::Smooth
                              ? field.heights
                              : decltype(field.heights){};
    const std::function<float(int, int)> sample = [&](int x, int z) {
      return snapshot[field.index(x, z)];
    };
    for (int z = affected.minZ;; ++z) {
      if (stop.stop_requested())
        return false;
      for (int x = affected.minX;; ++x) {
        if ((x & 255) == 0 && stop.stop_requested())
          return false;
        const auto sampleIndex = field.index(x, z);
        if (!protectedSamples[sampleIndex]) {
          const float previous = std::as_const(field.heights)[sampleIndex];
          const float height =
              applyTerrainEdit(edit, field.position(x, z), previous, sample, x,
                               z, field.cellsX, field.cellsZ);
          if (height != previous)
            field.heights.set(sampleIndex, height);
        }
        if (x == affected.maxX)
          break;
      }
      ++completed;
      report();
      if (z == affected.maxZ)
        break;
    }
  }
  if (!recomputeTerrainNormals(field, stop))
    return false;
  completed += rows;
  report();
  return !stop.stop_requested();
}

bool TerrainGenerator::recomputeTerrainNormals(HeightField &field,
                                               std::stop_token stop) {
  if (field.cellsX <= 0 || field.cellsZ <= 0 ||
      field.heights.size() !=
          (std::size_t(field.cellsX) + 1) * (std::size_t(field.cellsZ) + 1))
    throw std::invalid_argument("Terrain normals require a valid heightfield");
  field.normals.resize(field.heights.size());
  for (int z = 0;; ++z) {
    if (stop.stop_requested())
      return false;
    for (int x = 0;; ++x) {
      if ((x & 255) == 0 && stop.stop_requested())
        return false;
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
      field.normals.set(
          field.index(x, z),
          {float(-dx / length), float(1 / length), float(-dz / length)});
      if (x == field.cellsX)
        break;
    }
    if (z == field.cellsZ)
      break;
  }
  return !stop.stop_requested();
}

std::optional<HeightField>
TerrainGenerator::generate(const TerrainRecipe &recipe, std::stop_token stop,
                           const Progress &progress) {
  if (!recipe.graph.is_null())
    return executeTerrainGraph(recipe, {}, stop, progress);
  const auto baseProgress = [&](float value) {
    if (progress)
      progress(value * 0.5F);
  };
  auto field = generateBase(recipe, stop, baseProgress);
  if (!field)
    return std::nullopt;
  const auto surfaceProgress = [&](float value) {
    if (progress)
      progress(0.5F + value * 0.5F);
  };
  if (!applyTerrainSurfaceLayers(*field, recipe, stop, surfaceProgress))
    return std::nullopt;
  return field;
}

std::optional<HeightField>
TerrainGenerator::generate(const TerrainRecipe &recipe,
                           const TerrainPalette *palette, std::stop_token stop,
                           const Progress &progress,
                           std::string_view inputFingerprint) {
  if (!recipe.graph.is_null())
    return executeTerrainGraph(
        recipe, {palette, std::string(inputFingerprint), {}}, stop, progress);
  auto field = generate(recipe, stop, progress);
  if (!field)
    return field;
  field->inputFingerprint = inputFingerprint;
  if (palette == nullptr)
    return field;
  // Scattering reads the finished surface and biome assignment, so it runs
  // after generation and sculpting rather than beside them. A recipe that names
  // a palette but was handed none still generates; the missing asset is the
  // caller's to report, not a generation failure.
  if (recipe.paletteId.empty())
    return field;
  if (stop.stop_requested())
    return std::nullopt;
  field->paletteId = palette->id;
  field->resolvedPalette = std::make_shared<const TerrainPalette>(*palette);
  field->inputFingerprint = inputFingerprint;
  auto scattered = scatterTerrain(*field, recipe, *palette);
  field->scatterPlacements = std::move(scattered.placements);
  field->scatterTruncated = scattered.truncated;
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
  // A flat probe surface, so the snapshot records the base level rather than
  // the shape it happens to sit on.
  validation.landforms.at("default").heightVariation = 0;
  validation.landforms.at("default").octaves = 1;
  validation.landforms.at("default").featureSize =
      std::max(field.size.x, field.size.y);
  validation.edits.push_back(edit);
  validation.validate();
  if (field.heights.size() != validation.sampleCount())
    throw std::invalid_argument(
        "Protection source height array does not match grid");
  if (strength == 0)
    return edit;
  // Round outward to include every grid sample in the radial brush, then clamp
  // in double precision before converting potentially distant brush bounds.
  auto firstSample = [](float center, float extent, float size, int cells) {
    const double coordinate =
        std::floor((double(center) - extent) / size * cells);
    return int(std::clamp(coordinate, 0.0, double(cells)));
  };
  auto lastSample = [](float center, float extent, float size, int cells) {
    const double coordinate =
        std::ceil((double(center) + extent) / size * cells);
    return int(std::clamp(coordinate, 0.0, double(cells)));
  };
  const int firstX = firstSample(center.x, radius, field.size.x, field.cellsX);
  const int lastX = lastSample(center.x, radius, field.size.x, field.cellsX);
  const int firstZ = firstSample(center.y, radius, field.size.y, field.cellsZ);
  const int lastZ = lastSample(center.y, radius, field.size.y, field.cellsZ);
  for (int z = firstZ;; ++z) {
    for (int x = firstX;; ++x) {
      const auto position = field.position(x, z);
      if (terrainBrushWeight(position, center, radius, strength, falloff) > 0)
        edit.samples.push_back({position, field.height(x, z)});
      if (x == lastX)
        break;
    }
    if (z == lastZ)
      break;
  }
  validation.edits.back() = edit;
  validation.validate();
  return edit;
}
} // namespace demi::runtime
