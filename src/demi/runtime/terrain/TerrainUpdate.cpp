#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainEvaluation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace demi::runtime {
namespace {
bool same(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
bool same(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool same(Color a, Color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
bool same(const TerrainRegion &a, const TerrainRegion &b) {
  return a.biome == b.biome && same(a.center, b.center) &&
         a.radius == b.radius && a.strength == b.strength &&
         a.falloff == b.falloff;
}
bool same(const TerrainExclusion &a, const TerrainExclusion &b) {
  return same(a.center, b.center) && a.radius == b.radius &&
         a.strength == b.strength && a.falloff == b.falloff &&
         a.value == b.value;
}
bool same(const TerrainEdit &a, const TerrainEdit &b) {
  if (a.kind != b.kind || !same(a.center, b.center) || a.radius != b.radius ||
      a.strength != b.strength || a.falloff != b.falloff ||
      a.amount != b.amount || a.targetHeight != b.targetHeight ||
      !same(a.snapshotSize, b.snapshotSize) ||
      a.snapshotCellsX != b.snapshotCellsX ||
      a.snapshotCellsZ != b.snapshotCellsZ ||
      a.samples.size() != b.samples.size())
    return false;
  for (std::size_t index = 0; index < a.samples.size(); ++index)
    if (!same(a.samples[index].position, b.samples[index].position) ||
        a.samples[index].height != b.samples[index].height)
      return false;
  return true;
}
TerrainRect intersect(TerrainRect a, TerrainRect b) {
  if (a.empty() || b.empty())
    return {};
  return {std::max(a.minX, b.minX), std::max(a.minZ, b.minZ),
          std::min(a.maxX, b.maxX), std::min(a.maxZ, b.maxZ)};
}
TerrainRect brushBounds(const HeightField &field, Vec2 center, float radius) {
  const double minimumX = double(center.x) - radius;
  const double maximumX = double(center.x) + radius;
  const double minimumZ = double(center.y) - radius;
  const double maximumZ = double(center.y) + radius;
  if (maximumX < 0 || maximumZ < 0 || minimumX > field.size.x ||
      minimumZ > field.size.y)
    return {};
  const auto sample = [](double coordinate, float extent, int cells,
                         bool upper) {
    const double value = coordinate / extent * cells;
    return int(std::clamp(upper ? std::ceil(value) : std::floor(value), 0.0,
                          double(cells)));
  };
  return TerrainRect{sample(minimumX, field.size.x, field.cellsX, false),
                     sample(minimumZ, field.size.y, field.cellsZ, false),
                     sample(maximumX, field.size.x, field.cellsX, true),
                     sample(maximumZ, field.size.y, field.cellsZ, true)}
      .expanded(1, field.cellsX, field.cellsZ);
}
template <class Stroke>
TerrainRect changedBounds(const HeightField &field,
                          const std::vector<const Stroke *> &before,
                          const std::vector<const Stroke *> &after) {
  std::size_t prefix = 0;
  while (prefix < before.size() && prefix < after.size() &&
         same(*before[prefix], *after[prefix]))
    ++prefix;
  std::size_t oldEnd = before.size(), newEnd = after.size();
  while (oldEnd > prefix && newEnd > prefix &&
         same(*before[oldEnd - 1], *after[newEnd - 1])) {
    --oldEnd;
    --newEnd;
  }
  TerrainRect changed;
  for (std::size_t index = prefix; index < oldEnd; ++index)
    changed.include(
        brushBounds(field, before[index]->center, before[index]->radius));
  for (std::size_t index = prefix; index < newEnd; ++index)
    changed.include(
        brushBounds(field, after[index]->center, after[index]->radius));
  return changed;
}
bool sameBaseParameters(const TerrainRecipe &before,
                        const TerrainRecipe &after) {
  if (!same(before.size, after.size) || before.cellsX != after.cellsX ||
      before.cellsZ != after.cellsZ || before.seed != after.seed ||
      before.defaultBiome != after.defaultBiome ||
      before.biomes.size() != after.biomes.size())
    return false;
  for (const auto &[id, oldBiome] : before.biomes) {
    const auto found = after.biomes.find(id);
    if (found == after.biomes.end())
      return false;
    const auto &biome = found->second;
    if (oldBiome.baseHeight != biome.baseHeight ||
        oldBiome.heightVariation != biome.heightVariation ||
        oldBiome.featureSize != biome.featureSize ||
        oldBiome.roughness != biome.roughness ||
        oldBiome.octaves != biome.octaves)
      return false;
  }
  return true;
}
TerrainSampleValue sample(const HeightField &field, std::size_t index) {
  return {field.baseHeights.at(index), field.heights.at(index),
          field.exclusions.at(index), field.normals.at(index),
          field.biomeIndices.at(index)};
}
bool same(const TerrainSampleValue &a, const TerrainSampleValue &b) {
  return a.base == b.base && a.height == b.height &&
         a.exclusion == b.exclusion && a.biome == b.biome &&
         same(a.normal, b.normal);
}
template <class Work>
bool visit(const TerrainRect &area, std::stop_token stop, Work work) {
  if (area.empty())
    return !stop.stop_requested();
  for (int z = area.minZ;; ++z) {
    if (stop.stop_requested())
      return false;
    for (int x = area.minX;; ++x) {
      if ((x & 255) == 0 && stop.stop_requested())
        return false;
      work(x, z);
      if (x == area.maxX)
        break;
    }
    if (z == area.maxZ)
      break;
  }
  return true;
}
TerrainRect propagateSmoothing(TerrainRect changed, const HeightField &field,
                               const std::vector<const TerrainEdit *> &edits) {
  for (const auto *edit : edits) {
    if (edit->kind != TerrainEditKind::Smooth || edit->strength == 0)
      continue;
    changed.include(intersect(brushBounds(field, edit->center, edit->radius),
                              changed.expanded(1, field.cellsX, field.cellsZ)));
  }
  return changed;
}

bool replayLocal(const TerrainEvaluation &evaluation, HeightField &field,
                 TerrainRect output, std::stop_token stop,
                 TerrainUpdateStats &stats) {
  if (output.empty())
    return true;
  auto input = output;
  for (auto edit = evaluation.edits().rbegin();
       edit != evaluation.edits().rend(); ++edit) {
    if ((*edit)->kind == TerrainEditKind::Smooth && (*edit)->strength != 0 &&
        brushBounds(field, (*edit)->center, (*edit)->radius).intersects(input))
      input = input.expanded(1, field.cellsX, field.cellsZ);
  }
  const std::size_t width = std::size_t(input.maxX) - input.minX + 1;
  const std::size_t count = width * (std::size_t(input.maxZ) - input.minZ + 1);
  std::vector<float> heights(count);
  std::vector<unsigned char> protectedSamples(count, 0);
  const auto localIndex = [&](int x, int z) {
    return std::size_t(z - input.minZ) * width + x - input.minX;
  };
  const HeightField &read = field;
  if (!visit(input, stop, [&](int x, int z) {
        heights[localIndex(x, z)] = read.baseHeights[field.index(x, z)];
      }))
    return false;
  for (const auto *edit : evaluation.edits()) {
    if (stop.stop_requested())
      return false;
    const auto affected =
        intersect(input, brushBounds(field, edit->center, edit->radius));
    if (affected.empty())
      continue;
    if (edit->kind == TerrainEditKind::Protect) {
      for (const auto &saved : edit->samples) {
        if (stop.stop_requested())
          return false;
        const int x = int(std::llround(double(saved.position.x) / field.size.x *
                                       field.cellsX));
        const int z = int(std::llround(double(saved.position.y) / field.size.y *
                                       field.cellsZ));
        if (!input.contains(x, z))
          continue;
        heights[localIndex(x, z)] = saved.height;
        protectedSamples[localIndex(x, z)] = 1;
        ++stats.editEvaluations;
      }
      continue;
    }
    const auto snapshot =
        edit->kind == TerrainEditKind::Smooth ? heights : std::vector<float>{};
    const auto neighbour = [&](int x, int z) {
      // The backward halo prevents these outside checkpoint values from
      // influencing output. Boundary scratch values are discarded after replay.
      return input.contains(x, z) ? snapshot[localIndex(x, z)]
                                  : read.baseHeights[field.index(x, z)];
    };
    if (!visit(affected, stop, [&](int x, int z) {
          const auto index = localIndex(x, z);
          if (protectedSamples[index])
            return;
          heights[index] =
              applyTerrainEdit(*edit, field.position(x, z), heights[index],
                               neighbour, x, z, field.cellsX, field.cellsZ);
          ++stats.editEvaluations;
        }))
      return false;
  }
  return visit(output, stop, [&](int x, int z) {
    const auto index = field.index(x, z);
    const auto value = heights[localIndex(x, z)];
    if (read.heights[index] != value)
      field.heights.set(index, value);
  });
}
} // namespace

std::optional<TerrainUpdate>
regenerateTerrain(const TerrainRecipe &recipe,
                  std::shared_ptr<const HeightField> previous,
                  std::string reason, std::stop_token stop,
                  const TerrainGenerator::Progress &progress) {
  auto generated = TerrainGenerator::generate(recipe, stop, progress);
  if (!generated)
    return std::nullopt;
  auto field = std::make_shared<const HeightField>(std::move(*generated));
  auto patch = std::make_shared<TerrainPatch>();
  patch->fullBefore = std::move(previous);
  patch->fullAfter = field;
  patch->invalidation.fullGeneration = true;
  patch->invalidation.layoutChanged =
      !patch->fullBefore || !same(patch->fullBefore->size, field->size) ||
      patch->fullBefore->cellsX != field->cellsX ||
      patch->fullBefore->cellsZ != field->cellsZ ||
      patch->fullBefore->chunks.size() != field->chunks.size();
  patch->invalidation.reason = std::move(reason);
  patch->invalidation.baseSamples = patch->invalidation.heightSamples =
      patch->invalidation.normalSamples = patch->invalidation.biomeSamples =
          patch->invalidation.exclusionSamples = {0, 0, field->cellsX,
                                                  field->cellsZ};
  patch->invalidation.materialsChanged = true;
  return TerrainUpdate{field,
                       patch,
                       patch->invalidation,
                       {.baseEvaluations = field->heights.size(),
                        .normalEvaluations = field->heights.size(),
                        .changedSamples = field->heights.size()}};
}

std::optional<TerrainUpdate>
updateTerrain(const TerrainRecipe &before, const TerrainRecipe &after,
              std::shared_ptr<const HeightField> previous, std::stop_token stop,
              const TerrainGenerator::Progress &progress) {
  if (stop.stop_requested())
    return std::nullopt;
  before.validate();
  after.validate();
  if (!previous)
    return regenerateTerrain(after, {}, "No retained base checkpoint", stop,
                             progress);
  if (!same(previous->size, before.size) || previous->cellsX != before.cellsX ||
      previous->cellsZ != before.cellsZ)
    throw std::invalid_argument(
        "Incremental terrain source does not match the retained grid");
  TerrainEvaluation oldEvaluation(before), evaluation(after);
  if (!sameBaseParameters(before, after) ||
      oldEvaluation.generationEnabled() != evaluation.generationEnabled())
    return regenerateTerrain(
        after, previous, "Global generation inputs changed", stop, progress);
  if (progress)
    progress(0);
  auto field = std::make_shared<HeightField>(*previous);
  const HeightField &read = *field;
  auto patch = std::make_shared<TerrainPatch>();
  patch->size = field->size;
  patch->cellsX = field->cellsX;
  patch->cellsZ = field->cellsZ;
  TerrainUpdateStats stats;
  const auto baseArea =
      changedBounds(*field, oldEvaluation.regions(), evaluation.regions());
  auto heightArea =
      changedBounds(*field, oldEvaluation.edits(), evaluation.edits());
  heightArea.include(baseArea);
  heightArea = propagateSmoothing(heightArea, *field, oldEvaluation.edits());
  heightArea = propagateSmoothing(heightArea, *field, evaluation.edits());
  const auto exclusionArea = changedBounds(*field, oldEvaluation.exclusions(),
                                           evaluation.exclusions());
  if (!visit(baseArea, stop, [&](int x, int z) {
        const auto index = field->index(x, z);
        const auto value = evaluation.baseSample(field->position(x, z));
        if (read.baseHeights[index] != value.height)
          field->baseHeights.set(index, value.height);
        if (read.biomeIndices[index] != value.biome)
          field->biomeIndices.set(index, value.biome);
        ++stats.baseEvaluations;
      }))
    return std::nullopt;
  if (progress)
    progress(.25F);
  if (!replayLocal(evaluation, *field, heightArea, stop, stats))
    return std::nullopt;
  TerrainRect actualHeightChanges;
  if (!visit(heightArea, stop, [&](int x, int z) {
        const auto index = field->index(x, z);
        if (read.heights[index] != previous->heights[index])
          actualHeightChanges.include(x, z);
      }))
    return std::nullopt;
  const auto normalsArea =
      actualHeightChanges.expanded(1, field->cellsX, field->cellsZ);
  if (!visit(normalsArea, stop, [&](int x, int z) {
        const int left = std::max(0, x - 1),
                  right = x < field->cellsX ? x + 1 : x;
        const int down = std::max(0, z - 1), up = z < field->cellsZ ? z + 1 : z;
        const double dx =
            (double(field->height(right, z)) - field->height(left, z)) /
            (double(right - left) * field->size.x / field->cellsX);
        const double dz =
            (double(field->height(x, up)) - field->height(x, down)) /
            (double(up - down) * field->size.y / field->cellsZ);
        const double length = std::hypot(dx, 1.0, dz);
        const Vec3 normal{float(-dx / length), float(1 / length),
                          float(-dz / length)};
        const auto index = field->index(x, z);
        if (!same(read.normals[index], normal))
          field->normals.set(index, normal);
        ++stats.normalEvaluations;
      }))
    return std::nullopt;
  if (progress)
    progress(.75F);
  if (!visit(exclusionArea, stop, [&](int x, int z) {
        const auto index = field->index(x, z);
        const float value = evaluation.exclusion(field->position(x, z));
        if (read.exclusions[index] != value)
          field->exclusions.set(index, value);
      }))
    return std::nullopt;
  std::size_t biomeIndex = 0;
  for (const auto &[id, biome] : after.biomes) {
    if (!same(field->biomeColors[biomeIndex], biome.color)) {
      if (!patch->beforePalette)
        patch->beforePalette =
            TerrainPalette{previous->biomeIds, previous->biomeColors};
      field->biomeColors[biomeIndex] = biome.color;
      patch->invalidation.materialsChanged = true;
    }
    ++biomeIndex;
  }
  if (patch->beforePalette)
    patch->afterPalette = TerrainPalette{field->biomeIds, field->biomeColors};
  TerrainRect dirty = heightArea;
  dirty.include(normalsArea);
  dirty.include(baseArea);
  dirty.include(exclusionArea);
  if (!visit(dirty, stop, [&](int x, int z) {
        const auto index = field->index(x, z);
        const auto old = sample(*previous, index),
                   value = sample(*field, index);
        if (same(old, value))
          return;
        patch->samples.push_back({index, old, value});
        if (old.base != value.base)
          patch->invalidation.baseSamples.include(x, z);
        if (old.height != value.height)
          patch->invalidation.heightSamples.include(x, z);
        if (!same(old.normal, value.normal))
          patch->invalidation.normalSamples.include(x, z);
        if (old.biome != value.biome)
          patch->invalidation.biomeSamples.include(x, z);
        if (old.exclusion != value.exclusion)
          patch->invalidation.exclusionSamples.include(x, z);
      }))
    return std::nullopt;
  if (before.chunkCells != after.chunkCells) {
    field->chunks.clear();
    for (int z = 0; z < after.cellsZ;) {
      const int depth = std::min(after.chunkCells, after.cellsZ - z);
      for (int x = 0; x < after.cellsX;) {
        const int width = std::min(after.chunkCells, after.cellsX - x);
        field->chunks.push_back({x, z, width, depth});
        x += width;
      }
      z += depth;
    }
    patch->invalidation.layoutChanged = true;
    patch->fullBefore = previous;
    patch->fullAfter = field;
    patch->samples.clear();
  }
  stats.changedSamples = patch->samples.size();
  patch->invalidation.reason = "Local dependency replay from retained base";
  if (progress)
    progress(1);
  if (stop.stop_requested())
    return std::nullopt;
  return TerrainUpdate{std::move(field), patch, patch->invalidation, stats};
}
} // namespace demi::runtime
