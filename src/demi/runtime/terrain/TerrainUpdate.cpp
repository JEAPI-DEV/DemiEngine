#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainBrushBounds.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainEvaluation.h"
#include "demi/runtime/terrain/TerrainScatter.h"

#include <algorithm>
#include <cmath>
#include <span>
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
bool biomeRuleLayersChanged(const TerrainRecipe &before,
                            const TerrainRecipe &after) {
  if (before.rules.empty() && after.rules.empty())
    return false;
  const auto layers = [](const TerrainRecipe &recipe) {
    std::vector<std::pair<std::string, bool>> active;
    for (const auto &layer : recipe.layers)
      if (layer.kind == TerrainLayerKind::Biome)
        active.emplace_back(layer.id, layer.enabled);
    return active;
  };
  if (layers(before) == layers(after))
    return false;
  if (after.graph.is_null())
    return true;
  const auto graph = TerrainGraph::parse(after.graph);
  for (const auto &id : graph.executionOrder())
    if (graph.node(id)->type == "biomes")
      return true;
  return false;
}
TerrainRect intersect(TerrainRect a, TerrainRect b) {
  if (a.empty() || b.empty())
    return {};
  return {std::max(a.minX, b.minX), std::max(a.minZ, b.minZ),
          std::min(a.maxX, b.maxX), std::min(a.maxZ, b.maxZ)};
}
TerrainRect brushBounds(const HeightField &field, Vec2 center, float radius) {
  return terrainBrushSampleBounds(field, center, radius);
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

TerrainGenerationInputs retainedInputs(
    const TerrainRecipe &recipe,
    const std::shared_ptr<const HeightField> &previous) {
  if (recipe.paletteId.empty())
    return {};
  if (!previous || !previous->resolvedPalette ||
      previous->resolvedPalette->id != recipe.paletteId ||
      previous->inputFingerprint.empty())
    throw std::invalid_argument(
        "Terrain update requires resolved palette inputs");
  return {previous->resolvedPalette, previous->inputFingerprint};
}

void validateResolvedInputs(const TerrainRecipe &recipe,
                            const TerrainGenerationInputs &inputs) {
  if (recipe.paletteId.empty() ? bool(inputs.palette)
                               : (!inputs.palette ||
                                  inputs.palette->id != recipe.paletteId ||
                                  inputs.fingerprint.empty()))
    throw std::invalid_argument(
        "Terrain inputs do not match the recipe palette");
}
} // namespace

std::optional<TerrainUpdate>
regenerateTerrain(const TerrainRecipe &recipe,
                  std::shared_ptr<const HeightField> previous,
                  std::string reason, std::stop_token stop,
                  const TerrainGenerator::Progress &progress) {
  auto inputs = retainedInputs(recipe, previous);
  return regenerateTerrainWithInputs(recipe, std::move(previous),
                                     std::move(reason), inputs, stop, progress);
}

std::optional<TerrainUpdate>
regenerateTerrainWithInputs(const TerrainRecipe &recipe,
                            std::shared_ptr<const HeightField> previous,
                            std::string reason,
                            const TerrainGenerationInputs &inputs,
                            std::stop_token stop,
                            const TerrainGenerator::Progress &progress) {
  validateResolvedInputs(recipe, inputs);
  const auto previousCache =
      previous && previous->graphArtifacts ? previous->graphArtifacts->cache
                                           : nullptr;
  auto generated = !recipe.graph.is_null()
                       ? executeTerrainGraph(
                             recipe,
                             {inputs.palette.get(), inputs.fingerprint,
                              previousCache},
                             stop, progress)
                       : TerrainGenerator::generate(
                             recipe, inputs.palette.get(), stop, progress,
                             inputs.fingerprint);
  if (!generated)
    return std::nullopt;
  if (inputs.palette &&
      (generated->paletteId != recipe.paletteId ||
       generated->inputFingerprint != inputs.fingerprint ||
       !generated->resolvedPalette))
    throw std::logic_error("Terrain regeneration dropped resolved palette inputs");
  if (recipe.paletteId.empty()) {
    generated->scatterPlacements.clear();
    generated->scatterTruncated = false;
    generated->paletteId.clear();
    generated->resolvedPalette.reset();
    generated->inputFingerprint = inputs.fingerprint;
  }
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
  auto inputs = retainedInputs(after, previous);
  return updateTerrainWithInputs(before, after, std::move(previous), inputs,
                                 stop, progress);
}

std::optional<TerrainUpdate>
updateTerrainWithInputs(const TerrainRecipe &before, const TerrainRecipe &after,
                        std::shared_ptr<const HeightField> previous,
                        const TerrainGenerationInputs &inputs,
                        std::stop_token stop,
                        const TerrainGenerator::Progress &progress) {
  if (stop.stop_requested())
    return std::nullopt;
  before.validate();
  after.validate();
  validateResolvedInputs(after, inputs);
  if (!previous)
    return regenerateTerrainWithInputs(after, {},
                                       "No retained base checkpoint", inputs,
                                       stop, progress);
  if (!same(previous->size, before.size) || previous->cellsX != before.cellsX ||
      previous->cellsZ != before.cellsZ)
    throw std::invalid_argument(
        "Incremental terrain source does not match the retained grid");
  TerrainEvaluation oldEvaluation(before), evaluation(after);
  // Locality decision is owned by TerrainRecipe::sameGenerationInputs. A stage
  // that reaches beyond its own sample must be added there, so a stroke forces
  // a full rebuild rather than silently replaying with a seam.
  if (!before.sameGenerationInputs(after) ||
      biomeRuleLayersChanged(before, after) ||
      previous->inputFingerprint != inputs.fingerprint ||
      previous->paletteId != after.paletteId ||
      oldEvaluation.generationEnabled() != evaluation.generationEnabled())
    return regenerateTerrainWithInputs(after, previous,
                                       "Global generation inputs changed",
                                       inputs, stop, progress);
  const HeightField *graphBase = nullptr;
  if (!after.graph.is_null()) {
    graphBase = previous->graphArtifacts
                    ? previous->graphArtifacts->baseField.get()
                    : nullptr;
    if (graphBase == nullptr)
      return regenerateTerrainWithInputs(
          after, previous, "Graph base checkpoint is unavailable", inputs,
          stop, progress);
  }
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
  if (graphBase == nullptr)
    heightArea.include(baseArea);
  heightArea = propagateSmoothing(heightArea, *field, oldEvaluation.edits());
  heightArea = propagateSmoothing(heightArea, *field, evaluation.edits());
  const auto exclusionArea = changedBounds(*field, oldEvaluation.exclusions(),
                                           evaluation.exclusions());
  if (graphBase != nullptr) {
    TerrainGraphBiomeOverlay overlay(*graphBase, after);
    if (!visit(baseArea, stop, [&](int x, int z) {
          const auto index = field->index(x, z);
          const auto biome = overlay.biomeAt(x, z);
          if (read.biomeIndices[index] != biome)
            field->biomeIndices.set(index, biome);
          ++stats.baseEvaluations;
        }))
      return std::nullopt;
  } else if (!visit(baseArea, stop, [&](int x, int z) {
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
    if (!same(field->biomeColors[biomeIndex], biome.color) ||
        field->biomeMaterial(biomeIndex) != biome.material ||
        field->biomeTextureScale(biomeIndex) != biome.textureScale) {
      if (!patch->beforePalette)
        patch->beforePalette = TerrainBiomePalette{
            previous->biomeIds, previous->biomeColors,
            previous->biomeMaterials, previous->biomeTextureScales};
      if (field->biomeMaterials.empty())
        field->biomeMaterials.resize(field->biomeIds.size());
      if (field->biomeTextureScales.empty())
        field->biomeTextureScales.resize(field->biomeIds.size(), 1.F);
      field->biomeColors[biomeIndex] = biome.color;
      field->biomeMaterials[biomeIndex] = biome.material;
      field->biomeTextureScales[biomeIndex] = biome.textureScale;
      patch->invalidation.materialsChanged = true;
    }
    ++biomeIndex;
  }
  if (patch->beforePalette)
    patch->afterPalette = TerrainBiomePalette{
        field->biomeIds, field->biomeColors, field->biomeMaterials,
        field->biomeTextureScales};
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
  const bool changedSurfaceInputs =
      !heightArea.empty() || !baseArea.empty() || !exclusionArea.empty();
  if (changedSurfaceInputs && (graphBase != nullptr || inputs.palette)) {
    patch->beforeDerived = TerrainDerivedState{
        previous->scatterPlacements, previous->scatterTruncated,
        previous->graphArtifacts};
    if (graphBase != nullptr) {
      if (field->graphArtifacts &&
          field->graphArtifacts->waterResult &&
          !actualHeightChanges.empty()) {
        auto artifacts =
            std::make_shared<TerrainGraphArtifacts>(*field->graphArtifacts);
        auto refreshed = refreshTerrainWaterResult(
            *field, artifacts->water, field->heights, stop);
        if (!refreshed)
          return std::nullopt;
        if (refreshed->dropped)
          throw std::invalid_argument(
              "Terrain water bodies no longer match the edited surface");
        artifacts->waterResult = std::move(*refreshed);
        field->graphArtifacts = std::move(artifacts);
      }
      const auto &artifacts = *field->graphArtifacts;
      const auto candidates = artifacts.basePlacements
                                  ? std::span<const TerrainScatterPlacement>(
                                        *artifacts.basePlacements)
                                  : std::span<const TerrainScatterPlacement>{};
      if (!refreshTerrainScatterPlacements(
              *field, candidates,
              artifacts.water.bodies.empty() ? nullptr : &artifacts.water,
              stop))
        return std::nullopt;
    } else {
      auto candidates = scatterTerrain(*field, after, *inputs.palette);
      if (stop.stop_requested() ||
          !refreshTerrainScatterPlacements(*field, candidates.placements,
                                           nullptr, stop))
        return std::nullopt;
      field->scatterTruncated = candidates.truncated;
    }
    patch->afterDerived = TerrainDerivedState{
        field->scatterPlacements, field->scatterTruncated,
        field->graphArtifacts};
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
