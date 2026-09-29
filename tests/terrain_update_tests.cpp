#include "demi/runtime/terrain/TerrainUpdate.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>

using namespace demi::runtime;
namespace {
std::shared_ptr<const HeightField> generate(const TerrainRecipe &recipe) {
  return std::make_shared<const HeightField>(
      *TerrainGenerator::generate(recipe));
}
void equal(const HeightField &a, const HeightField &b) {
  assert(a.cellsX == b.cellsX && a.cellsZ == b.cellsZ);
  assert(a.baseHeights == b.baseHeights);
  assert(a.biomeIndices == b.biomeIndices);
  assert(a.exclusions == b.exclusions);
  for (std::size_t i = 0; i < a.heights.size(); ++i) {
    if (std::abs(a.heights[i] - b.heights[i]) > 1e-5F) {
      std::cerr << "Height mismatch at " << i << ": " << a.heights[i]
                << " != " << b.heights[i] << '\n';
      std::abort();
    }
    assert(std::abs(a.normals[i].x - b.normals[i].x) < 1e-5F);
    assert(std::abs(a.normals[i].y - b.normals[i].y) < 1e-5F);
    assert(std::abs(a.normals[i].z - b.normals[i].z) < 1e-5F);
  }
}
TerrainEdit stroke(TerrainEditKind kind, float x, float z, float radius = 3) {
  TerrainEdit edit;
  edit.kind = kind;
  edit.center = {x, z};
  edit.radius = radius;
  edit.amount = 2;
  edit.targetHeight = 4;
  return edit;
}
void localHistoryAndChannels() {
  TerrainRecipe before;
  before.size = {256, 256};
  before.cellsX = before.cellsZ = 256;
  auto initial = generate(before);
  auto after = before;
  after.edits.push_back(stroke(TerrainEditKind::Raise, 130, 130));
  auto update = updateTerrain(before, after, initial);
  assert(update && !update->invalidation.fullGeneration);
  assert(update->stats.baseEvaluations == 0);
  assert(update->stats.editEvaluations < 1000);
  assert(update->patch->samples.size() < 1000 && !update->patch->fullBefore);
  assert(update->field->heights.pageIdentity(0) ==
         initial->heights.pageIdentity(0));
  equal(*update->field, *generate(after));
  equal(*applyTerrainPatch(update->field, *update->patch, false).field,
        *initial);
  equal(*applyTerrainPatch(initial, *update->patch, true).field,
        *update->field);
  auto nextRecipe = after;
  nextRecipe.edits.push_back(stroke(TerrainEditKind::Smooth, 132, 130));
  const auto next = updateTerrain(after, nextRecipe, update->field);
  const auto combined = mergeTerrainPatches(*update->patch, *next->patch);
  equal(*applyTerrainPatch(initial, *combined, true).field,
        *generate(nextRecipe));
  equal(*applyTerrainPatch(next->field, *combined, false).field, *initial);
  assert(combined->retainedBytes() < initial->heights.size() * sizeof(float));

  auto tint = nextRecipe;
  tint.biomes.at("default").color = {.9F, .2F, .3F, 1};
  const auto tinted = updateTerrain(nextRecipe, tint, next->field);
  assert(tinted && tinted->invalidation.materialsChanged);
  assert(tinted->stats.baseEvaluations == 0 &&
         tinted->stats.normalEvaluations == 0);
  assert(tinted->invalidation.geometrySamples().empty() &&
         tinted->patch->samples.empty());
  assert(tinted->field->heights.pageIdentity(0) ==
         next->field->heights.pageIdentity(0));
  equal(*tinted->field, *next->field);
  auto excluded = tint;
  TerrainExclusion exclusion;
  exclusion.center = {130, 130};
  exclusion.radius = 3;
  excluded.exclusions.push_back(exclusion);
  const auto exclusionUpdate = updateTerrain(tint, excluded, tinted->field);
  assert(exclusionUpdate &&
         !exclusionUpdate->invalidation.exclusionSamples.empty());
  assert(exclusionUpdate->invalidation.geometrySamples().empty());
  assert(exclusionUpdate->stats.baseEvaluations == 0 &&
         exclusionUpdate->stats.normalEvaluations == 0);
  equal(*exclusionUpdate->field, *generate(excluded));
}
void dependenciesAndLayers() {
  TerrainRecipe recipe;
  recipe.size = {32, 32};
  recipe.cellsX = recipe.cellsZ = 32;
  recipe.biomes.emplace("hill", TerrainBiome{.baseHeight = 8});
  recipe.layers.push_back({"detail", "Detail", TerrainLayerKind::Sculpt, true});
  auto field = generate(recipe);
  std::mt19937 random(19);
  for (int step = 0; step < 45; ++step) {
    auto next = recipe;
    auto edit =
        stroke(step % 3 == 0 ? TerrainEditKind::Smooth : TerrainEditKind::Raise,
               float(random() % 30 + 1), float(random() % 30 + 1), 5);
    if (step % 2)
      edit.layer = "detail";
    if (step % 7 == 0 && !next.edits.empty())
      next.edits.erase(next.edits.begin());
    else if (step % 9 == 0)
      next.layers.back().enabled = !next.layers.back().enabled;
    else if (step % 11 == 0) {
      TerrainRegion region;
      region.biome = "hill";
      region.center = edit.center;
      region.radius = 4;
      next.regions.push_back(region);
    } else
      next.edits.push_back(edit);
    const auto changed = updateTerrain(recipe, next, field);
    assert(changed && !changed->invalidation.fullGeneration);
    equal(*changed->field, *generate(next));
    equal(*applyTerrainPatch(changed->field, *changed->patch, false).field,
          *field);
    recipe = std::move(next);
    field = changed->field;
  }
  auto protectedRecipe = recipe;
  protectedRecipe.edits.push_back(createProtectionEdit(*field, {16, 16}, 5));
  auto protectedUpdate = updateTerrain(recipe, protectedRecipe, field);
  equal(*protectedUpdate->field, *generate(protectedRecipe));
  auto smoother = protectedRecipe;
  smoother.edits.push_back(stroke(TerrainEditKind::Smooth, 16, 16, 10));
  auto smoothed =
      updateTerrain(protectedRecipe, smoother, protectedUpdate->field);
  equal(*smoothed->field, *generate(smoother));
  auto toggle = smoother;
  for (auto &layer : toggle.layers)
    if (layer.kind == TerrainLayerKind::Protection)
      layer.enabled = false;
  auto unprotected = updateTerrain(smoother, toggle, smoothed->field);
  equal(*unprotected->field, *generate(toggle));
}
void globalChangesAndCancellation() {
  TerrainRecipe before;
  before.cellsX = before.cellsZ = 16;
  auto initial = generate(before);
  auto after = before;
  after.seed += 1;
  auto update = updateTerrain(before, after, initial);
  assert(update && update->invalidation.fullGeneration &&
         update->patch->fullBefore);
  equal(*update->field, *generate(after));
  equal(*applyTerrainPatch(update->field, *update->patch, false).field,
        *initial);
  auto retiled = before;
  retiled.chunkCells = 4;
  const auto tileUpdate = updateTerrain(before, retiled, initial);
  assert(tileUpdate && tileUpdate->invalidation.layoutChanged &&
         tileUpdate->stats.baseEvaluations == 0);
  equal(*tileUpdate->field, *initial);
  after = before;
  after.edits.push_back(stroke(TerrainEditKind::Smooth, 50, 50, 10));
  std::stop_source cancellation;
  const auto cancelled = updateTerrain(
      before, after, initial, cancellation.get_token(), [&](float progress) {
        if (progress >= .25F)
          cancellation.request_stop();
      });
  assert(!cancelled);
  equal(*initial, *generate(before));
}
} // namespace
int main() {
  localHistoryAndChannels();
  dependenciesAndLayers();
  globalChangesAndCancellation();
  std::cout << "Terrain incremental update checks passed\n";
}
