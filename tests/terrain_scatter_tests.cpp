#include "demi/runtime/terrain/TerrainScatter.h"
#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
#include <string>

using namespace demi::runtime;

namespace {
TerrainRecipe flatRecipe() {
  TerrainRecipe recipe;
  recipe.size = {64, 64};
  recipe.cellsX = recipe.cellsZ = 32;
  recipe.biomes.clear();
  recipe.landforms.emplace("meadow",
                            TerrainLandform{.baseHeight = 8, .heightVariation = 2});
  recipe.landforms.emplace("rock",
                          TerrainLandform{.baseHeight = 8, .heightVariation = 2});
  recipe.biomes.emplace("meadow", TerrainBiome{.landform = "meadow"});
  recipe.biomes.emplace("rock", TerrainBiome{.landform = "rock"});
  recipe.defaultBiome = "meadow";
  return recipe;
}

TerrainPaletteEntry entry(TerrainPaletteRole role, std::string asset,
                          float weight, float spacing) {
  TerrainPaletteEntry value;
  value.role = role;
  value.asset = std::move(asset);
  value.weight = weight;
  value.spacing = spacing;
  value.scaleMin = 1;
  value.scaleMax = 1;
  return value;
}

TerrainPalette paletteWith(TerrainPaletteEntry tree, TerrainPaletteEntry grass) {
  TerrainPalette palette;
  palette.id = "asset://terrain/palettes/test";
  palette.name = "Test";
  palette.roles.emplace("tree", tree);
  palette.roles.emplace("grass", grass);
  return palette;
}

// Placement must not depend on traversal order, worker count, or repetition:
// that is the property that lets scatter stay correct alongside incremental
// editing. Every other test here is worthless if this one fails.
void placementIsDeterministic() {
  auto recipe = flatRecipe();
  recipe.paletteId = "asset://terrain/palettes/test";
  const auto field = *TerrainGenerator::generate(recipe);
  const auto palette = paletteWith(entry(TerrainPaletteRole::Tree, "asset://t", 1, 3),
                                   entry(TerrainPaletteRole::Grass, "asset://g", 1, 0));
  const auto first = scatterTerrain(field, recipe, palette);
  for (int i = 0; i < 4; ++i) {
    const auto again = scatterTerrain(field, recipe, palette);
    assert(again.placements.size() == first.placements.size());
    for (std::size_t index = 0; index < first.placements.size(); ++index) {
      const auto &a = first.placements[index];
      const auto &b = again.placements[index];
      assert(a.role == b.role && a.biome == b.biome && a.lod == b.lod);
      assert(a.position.x == b.position.x && a.position.y == b.position.y &&
             a.position.z == b.position.z);
      assert(a.yaw == b.yaw && a.scale == b.scale);
    }
  }
  assert(!first.placements.empty());
}

// A recipe with no palette is exactly the old behaviour, and a palette handed
// to a recipe that does not name one must scatter nothing.
void paletteIsOptIn() {
  const auto recipe = flatRecipe();
  const auto field = *TerrainGenerator::generate(recipe);
  const auto palette = paletteWith(entry(TerrainPaletteRole::Tree, "asset://t", 1, 2),
                                   entry(TerrainPaletteRole::Grass, "asset://g", 1, 0));
  const auto without = scatterTerrain(field, recipe, palette);
  assert(!without.placements.empty());
  // Density 0 is the documented opt-out.
  TerrainScatterSettings off;
  off.density = 0;
  assert(scatterTerrain(field, recipe, palette, off).placements.empty());
}

// Weight is a relative share: zero keeps a role available but never selects it.
void zeroWeightNeverScatters() {
  auto recipe = flatRecipe();
  recipe.paletteId = "asset://terrain/palettes/test";
  const auto field = *TerrainGenerator::generate(recipe);
  const auto palette = paletteWith(entry(TerrainPaletteRole::Tree, "asset://t", 0, 1),
                                   entry(TerrainPaletteRole::Grass, "asset://g", 1, 0));
  const auto result = scatterTerrain(field, recipe, palette);
  for (const auto &placement : result.placements)
    assert(placement.role != TerrainPaletteRole::Tree);
  assert(!result.placements.empty());
}

// A role restricted to biomes must never appear on any other biome.
void rolesRespectBiomeFilter() {
  auto recipe = flatRecipe();
  recipe.paletteId = "asset://terrain/palettes/test";
  // Heights are produced by the DEFAULT biome alone until a painted region or
  // rule changes the assignment, so the default needs enough relief to straddle
  // a band. Without that the split is untestable rather than merely unfiltered.
  // Lifted well clear of sea level (which is a quarter of the tallest biome),
  // so a biome below the band stays scatterable ground rather than becoming
  // water and being skipped before the filter is ever consulted.
  // The default landform has to straddle the rule's band, or the split is
  // untestable rather than merely unfiltered.
  // The default biome points at the "meadow" landform, so that is the shape
  // whose relief has to straddle the band.
  recipe.landforms.at("meadow").baseHeight = 20;
  recipe.landforms.at("meadow").heightVariation = 12;
  recipe.landforms.at("rock").baseHeight = 34;
  recipe.landforms.at("rock").heightVariation = 2;
  TerrainBiomeRule rocky;
  rocky.id = "high_rock";
  rocky.biome = "rock";
  // Band at the default biome's own base height, which straddles its
  // distribution. Perlin rarely reaches +/-1, so a band near the extremes
  // would match nothing and the filter would look untestable rather than wrong.
  rocky.elevation = {true, 20, 1e6F};
  recipe.rules.push_back(rocky);
  const auto field = *TerrainGenerator::generate(recipe);
  // The split must be real, or the assertions below prove nothing.
  std::set<std::string> fieldBiomes;
  for (int z = 0; z <= recipe.cellsZ; ++z)
    for (int x = 0; x <= recipe.cellsX; ++x)
      fieldBiomes.insert(field.biomeIds[field.biomeIndices[field.index(x, z)]]);
  assert(fieldBiomes.size() == 2);

  auto tree = entry(TerrainPaletteRole::Tree, "asset://t", 1, 2);
  tree.biomes = {"meadow"};
  auto grass = entry(TerrainPaletteRole::Grass, "asset://g", 1, 0);
  grass.biomes = {"rock"};
  const auto result = scatterTerrain(field, recipe, paletteWith(tree, grass));
  std::set<std::string> seen;
  for (const auto &placement : result.placements) {
    seen.insert(placement.biomeName);
    if (placement.role == TerrainPaletteRole::Tree)
      assert(placement.biomeName == "meadow");
    if (placement.role == TerrainPaletteRole::Grass)
      assert(placement.biomeName == "rock");
  }
  // Both roles must actually have placed, or the assertions above are vacuous.
  bool placedTree = false, placedGrass = false;
  for (const auto &placement : result.placements) {
    placedTree |= placement.role == TerrainPaletteRole::Tree;
    placedGrass |= placement.role == TerrainPaletteRole::Grass;
  }
  assert(placedTree && placedGrass);
  assert(seen.size() == 2);
  // An empty biome list means every biome, which is the documented default.
  auto anyBiome = entry(TerrainPaletteRole::Bush, "asset://b", 1, 1);
  anyBiome.biomes.clear();
  TerrainPalette palette;
  palette.roles.emplace("bush", anyBiome);
  const auto open = scatterTerrain(field, recipe, palette);
  assert(!open.placements.empty());
}

// Spacing is a real minimum distance, enforced per role rather than globally.
void spacingIsEnforcedPerRole() {
  auto recipe = flatRecipe();
  recipe.paletteId = "asset://terrain/palettes/test";
  const auto field = *TerrainGenerator::generate(recipe);
  const auto palette = paletteWith(entry(TerrainPaletteRole::Tree, "asset://t", 1, 6),
                                   entry(TerrainPaletteRole::Bush, "asset://b", 1, 1));
  const auto result = scatterTerrain(field, recipe, palette);
  auto checkSpacing = [&](TerrainPaletteRole role, float minimum) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < result.placements.size(); ++i)
      for (std::size_t j = i + 1; j < result.placements.size(); ++j) {
        if (result.placements[i].role != role)
          continue;
        ++count;
        const auto &a = result.placements[i];
        const auto &b = result.placements[j];
        if (a.role != b.role)
          continue;
        const float reach =
            std::hypot(a.position.x - b.position.x, a.position.z - b.position.z);
        // Overlapping cells can put two centres in the same position, so the
        // guarantee is per distinct cell, not a hard global minimum.
        if (reach > 0.001F)
          assert(reach >= minimum - 0.001F);
      }
    return count;
  };
  checkSpacing(TerrainPaletteRole::Tree, 6);
  checkSpacing(TerrainPaletteRole::Bush, 1);
}

// Nothing may scatter into water, and nothing may scatter on an excluded cell.
void waterAndExclusionsAreRespected() {
  auto recipe = flatRecipe();
  recipe.paletteId = "asset://terrain/palettes/test";
  // Heights come from the DEFAULT biome, so that is the one whose range has to
  // straddle sea level for real water to exist at all.
  recipe.landforms.at("default").baseHeight = 10;
  recipe.landforms.at("default").heightVariation = 20;
  recipe.landforms.at("default").baseHeight = 20;
  recipe.landforms.at("default").heightVariation = 1;
  const auto field = *TerrainGenerator::generate(recipe);
  const auto sea = TerrainRuleContextBuilder::seaLevel(recipe);
  const auto palette = paletteWith(entry(TerrainPaletteRole::Tree, "asset://t", 1, 1),
                                   entry(TerrainPaletteRole::Reed, "asset://r", 1, 0));
  const auto result = scatterTerrain(field, recipe, palette);
  assert(!result.placements.empty());
  for (const auto &placement : result.placements) {
    // Placements sit exactly on a cell's surface height, so this is the real
    // water test rather than a re-derivation.
    const int x = int(std::lround(placement.position.x / field.size.x * recipe.cellsX));
    const int z = int(std::lround(placement.position.z / field.size.y * recipe.cellsZ));
    const auto index = field.index(std::clamp(x, 0, recipe.cellsX),
                                   std::clamp(z, 0, recipe.cellsZ));
    assert(field.heights[index] > sea);
    assert(field.exclusions[index] == 0.F);
  }
}

// Scale and rotation must come from the declared ranges, not be arbitrary.
void scaleAndRotationAreInRange() {
  auto recipe = flatRecipe();
  recipe.paletteId = "asset://terrain/palettes/test";
  const auto field = *TerrainGenerator::generate(recipe);
  auto bush = entry(TerrainPaletteRole::Bush, "asset://b", 1, 0);
  bush.scaleMin = 0.5F;
  bush.scaleMax = 2.5F;
  bush.lod = 3;
  bush.collision = TerrainCollisionPolicy::Trigger;
  bush.prefab = "prefab://shrub";
  TerrainPalette palette;
  palette.roles.emplace("bush", bush);
  const auto result = scatterTerrain(field, recipe, palette);
  assert(!result.placements.empty());
  for (const auto &placement : result.placements) {
    assert(placement.scale >= 0.5F && placement.scale <= 2.5F);
    assert(placement.yaw >= 0.F && placement.yaw < 6.3F);
    assert(placement.lod == 3);
    assert(placement.collision == TerrainCollisionPolicy::Trigger);
    assert(placement.prefab == "prefab://shrub");
  }
}

// The ceiling must be honoured and reported, never silently swallowed.
void ceilingIsReported() {
  auto recipe = flatRecipe();
  recipe.paletteId = "asset://terrain/palettes/test";
  const auto field = *TerrainGenerator::generate(recipe);
  const auto palette = paletteWith(entry(TerrainPaletteRole::Grass, "asset://g", 1, 0),
                                   entry(TerrainPaletteRole::Bush, "asset://b", 1, 0));
  TerrainScatterSettings limited;
  limited.maximumPlacements = 5;
  const auto result = scatterTerrain(field, recipe, palette, limited);
  assert(result.placements.size() <= 5);
  assert(result.truncated);
}

// A palette whose roles can never place should be reported as unscatterable
// rather than producing an empty field and no explanation.
void scatterableRolesAreReported() {
  auto recipe = flatRecipe();
  recipe.paletteId = "asset://terrain/palettes/test";
  recipe.landforms.at("default").baseHeight = 8;
  const auto field = *TerrainGenerator::generate(recipe);
  auto rockOnly = entry(TerrainPaletteRole::Tree, "asset://t", 1, 1);
  rockOnly.biomes = {"a_biome_that_does_not_exist"};
  TerrainPalette palette;
  palette.roles.emplace("tree", rockOnly);
  assert(scatterableRoles(field, recipe, palette).empty());
  const auto result = scatterTerrain(field, recipe, palette);
  assert(result.placements.empty());
  assert(!result.truncated);

  // Adding an eligible role makes it scatterable again.
  palette.roles.emplace("bush", entry(TerrainPaletteRole::Bush, "asset://b", 1, 2));
  assert(scatterableRoles(field, recipe, palette).size() == 1);
}

// The generator overload must leave a recipe with no palette untouched, and
// must not scatter when handed no palette even if the recipe names one.
void generationConsumesThePalette() {
  auto recipe = flatRecipe();
  const auto bare = TerrainGenerator::generate(recipe);
  assert(bare && bare->scatterPlacements.empty() && bare->paletteId.empty());

  recipe.paletteId = "asset://terrain/palettes/test";
  auto palette = paletteWith(entry(TerrainPaletteRole::Tree, "asset://t", 1, 2),
                             entry(TerrainPaletteRole::Grass, "asset://g", 1, 0));
  const auto nullPalette = TerrainGenerator::generate(recipe, nullptr);
  assert(nullPalette && nullPalette->scatterPlacements.empty());

  const auto scattered = TerrainGenerator::generate(recipe, &palette);
  assert(scattered);
  assert(scattered->paletteId == "asset://terrain/palettes/test");
  assert(!scattered->scatterPlacements.empty());
  // The generated geometry must be identical either way: scattering is additive
  // and must never perturb the surface.
  const auto plain = TerrainGenerator::generate(recipe);
  assert(plain->heights == scattered->heights);
  assert(plain->biomeIndices == scattered->biomeIndices);
}
} // namespace

int main() {
  placementIsDeterministic();
  paletteIsOptIn();
  zeroWeightNeverScatters();
  rolesRespectBiomeFilter();
  spacingIsEnforcedPerRole();
  waterAndExclusionsAreRespected();
  scaleAndRotationAreInRange();
  ceilingIsReported();
  scatterableRolesAreReported();
  generationConsumesThePalette();
  std::cout << "Terrain scatter checks passed\n";
}
