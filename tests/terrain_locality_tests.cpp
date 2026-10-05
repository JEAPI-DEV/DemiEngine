#include "demi/runtime/terrain/TerrainUpdate.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace demi::runtime;

namespace {
std::shared_ptr<const HeightField> generate(const TerrainRecipe &recipe) {
  return std::make_shared<const HeightField>(
      *TerrainGenerator::generate(recipe));
}

TerrainRecipe base() {
  TerrainRecipe recipe;
  recipe.size = {48, 48};
  recipe.cellsX = recipe.cellsZ = 48;
  recipe.landforms.emplace("peak",
                            TerrainLandform{.baseHeight = 9, .featureSize = 20});
  recipe.biomes.emplace("peak", TerrainBiome{.landform = "peak"});
  return recipe;
}

// The locality gate has two halves: the recipe classifier owns generation
// inputs stored as recipe fields, and TerrainEvaluation::generationEnabled
// owns the generation layer toggle. A new nonlocal stage or generation
// parameter must be added to one of them; the matching case here has to be
// added too. That is the tripwire milestone 1 relies on.
void generationInputsForceFullRebuild() {
  const auto origin = generate(base());
  struct Case {
    const char *name;
    TerrainRecipe (*mutate)(TerrainRecipe);
    bool classifiedByRecipe;
  };
  const std::vector<Case> cases{
      {"seed", [](TerrainRecipe r) { r.seed += 1; return r; }, true},
      {"size", [](TerrainRecipe r) { r.size = {64, 48}; return r; }, true},
      {"sizeX", [](TerrainRecipe r) { r.size.x = 60; return r; }, true},
      {"sizeY", [](TerrainRecipe r) { r.size.y = 60; return r; }, true},
      {"defaultBiome", [](TerrainRecipe r) { r.defaultBiome = "peak"; return r; }, true},
      {"biomeAdded",
       [](TerrainRecipe r) {
         r.landforms.emplace("hill", TerrainLandform{.baseHeight = 4});
         r.biomes.emplace("hill", TerrainBiome{.landform = "hill"});
         return r;
       },
       true},
      {"biomeRemoved",
       [](TerrainRecipe r) {
         r.biomes.erase("peak");
         return r;
       },
       true},
      {"biomeBaseHeight",
       [](TerrainRecipe r) { r.landforms.at("default").baseHeight += 1; return r; }, true},
      {"biomeHeightVariation",
       [](TerrainRecipe r) { r.landforms.at("default").heightVariation += 1; return r; },
       true},
      {"biomeFeatureSize",
       [](TerrainRecipe r) { r.landforms.at("default").featureSize += 1; return r; }, true},
      {"biomeRoughness",
       [](TerrainRecipe r) { r.landforms.at("default").roughness += .1F; return r; }, true},
      {"biomeOctaves",
       [](TerrainRecipe r) { r.landforms.at("default").octaves += 1; return r; }, true},
      // Layers are deliberately absent from the recipe classifier, so this
      // case must be caught by the evaluator half of the gate instead.
      {"generationLayerDisabled",
       [](TerrainRecipe r) {
         for (auto &layer : r.layers)
           if (layer.kind == TerrainLayerKind::Generation)
             layer.enabled = false;
         return r;
       },
       false},
  };
  for (const auto &entry : cases) {
    const auto before = base();
    const auto after = entry.mutate(before);
    assert(before.sameGenerationInputs(after) != entry.classifiedByRecipe);
    auto update = updateTerrain(before, after, origin);
    assert(update && update->invalidation.fullGeneration);
    assert(update->patch->fullBefore);
    std::cout << "  full rebuild: " << entry.name
              << (entry.classifiedByRecipe ? " (recipe)" : " (evaluator)")
              << std::endl;
  }
}

// Resolution is a generation input: changing it must rebuild the whole base at
// the new grid rather than replay locally onto the old one.
void resolutionForcesFullRebuild() {
  const auto origin = generate(base());
  const auto before = base();
  auto after = before;
  after.cellsX = 32;
  after.cellsZ = 32;
  assert(!before.sameGenerationInputs(after));
  auto update = updateTerrain(before, after, origin);
  assert(update && update->invalidation.fullGeneration);
  assert(update->field->cellsX == 32 && update->field->cellsZ == 32);
  const auto reference = generate(after);
  assert(update->field->heights == reference->heights);
  std::cout << "  full rebuild: resolution (recipe)\n";
}

// A retained field that does not match the old recipe is a broken checkpoint
// and must be rejected, not silently replayed onto.
void mismatchedCheckpointIsRejected() {
  const auto before = base();
  auto foreign = base();
  foreign.cellsX = foreign.cellsZ = 16;
  foreign.size = {16, 16};
  auto after = before;
  TerrainEdit edit;
  edit.kind = TerrainEditKind::Raise;
  edit.center = {8, 8};
  edit.radius = 2;
  edit.amount = 1;
  after.edits.push_back(edit);
  bool rejected = false;
  try {
    (void)updateTerrain(before, after, generate(foreign));
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  std::cout << "  rejected:      mismatched retained checkpoint\n";
}

// Surface-only and layout-only inputs must stay local. If one of these ever
// forces a full rebuild, incremental editing has silently regressed.
void surfaceInputsStayLocal() {
  const auto origin = generate(base());
  struct Case {
    const char *name;
    TerrainRecipe (*mutate)(TerrainRecipe, const HeightField &);
  };
  const std::vector<Case> cases{
      {"raiseEdit",
       [](TerrainRecipe r, const HeightField &) {
         TerrainEdit edit;
         edit.kind = TerrainEditKind::Raise;
         edit.center = {20, 20};
         edit.radius = 4;
         edit.amount = 1;
         r.edits.push_back(edit);
         return r;
       }},
      {"protectionEdit",
       [](TerrainRecipe r, const HeightField &field) {
         r.edits.push_back(createProtectionEdit(field, {20, 20}, 4));
         return r;
       }},
      {"biomeRegion",
       [](TerrainRecipe r, const HeightField &) {
         TerrainRegion region;
         region.biome = "peak";
         region.center = {20, 20};
         region.radius = 5;
         r.regions.push_back(region);
         return r;
       }},
      {"exclusion",
       [](TerrainRecipe r, const HeightField &) {
         TerrainExclusion exclusion;
         exclusion.center = {20, 20};
         exclusion.radius = 4;
         r.exclusions.push_back(exclusion);
         return r;
       }},
      {"biomeTint",
       [](TerrainRecipe r, const HeightField &) {
         r.biomes.at("peak").color = {1, 0, 0, 1};
         return r;
       }},
      {"sculptLayerDisabled",
       [](TerrainRecipe r, const HeightField &) {
         for (auto &layer : r.layers)
           if (layer.kind == TerrainLayerKind::Sculpt)
             layer.enabled = false;
         return r;
       }},
      {"biomeLayerDisabled",
       [](TerrainRecipe r, const HeightField &) {
         for (auto &layer : r.layers)
           if (layer.kind == TerrainLayerKind::Biome)
             layer.enabled = false;
         return r;
       }},
      {"chunkCells",
       [](TerrainRecipe r, const HeightField &) {
         r.chunkCells = 8;
         return r;
       }},
  };
  for (const auto &entry : cases) {
    const auto before = base();
    const auto after = entry.mutate(before, *origin);
    assert(before.sameGenerationInputs(after));
    auto update = updateTerrain(before, after, origin);
    assert(update && !update->invalidation.fullGeneration);
    std::cout << "  local replay:  " << entry.name << std::endl;
  }
}

// chunkCells changes layout only: heights are untouched, so a local replay must
// reproduce the previous heights exactly while rebuilding the chunk partition.
void chunkCellsChangeLayoutNotHeights() {
  const auto origin = generate(base());
  const auto before = base();
  auto after = before;
  after.chunkCells = 8;
  auto update = updateTerrain(before, after, origin);
  assert(update && !update->invalidation.fullGeneration);
  assert(update->invalidation.layoutChanged);
  assert(update->field->heights == origin->heights);
  assert(update->field->chunks.size() != origin->chunks.size());
  std::cout << "  layout-only:   chunkCells\n";
}
} // namespace

int main() {
  generationInputsForceFullRebuild();
  resolutionForcesFullRebuild();
  mismatchedCheckpointIsRejected();
  surfaceInputsStayLocal();
  chunkCellsChangeLayoutNotHeights();
  std::cout << "Terrain locality contract checks passed\n";
}
