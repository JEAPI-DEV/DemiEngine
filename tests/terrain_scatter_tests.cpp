#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainScatter.h"
#include "demi/runtime/terrain/TerrainScatterConstraints.h"
#include "demi/runtime/terrain/TerrainScatterLayout.h"
#include "demi/runtime/terrain/TerrainWater.h"
#include <algorithm>
#include <bit>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

using namespace demi::runtime;

namespace {
TerrainRecipe flatRecipe() {
  TerrainRecipe recipe;
  recipe.size = {64, 64};
  recipe.cellsX = recipe.cellsZ = 32;
  recipe.biomes.clear();
  recipe.landforms.emplace(
      "meadow", TerrainLandform{.baseHeight = 8, .heightVariation = 2});
  recipe.landforms.emplace(
      "rock", TerrainLandform{.baseHeight = 8, .heightVariation = 2});
  recipe.biomes.emplace("meadow", TerrainBiome{.landform = "meadow"});
  recipe.biomes.emplace("rock", TerrainBiome{.landform = "rock"});
  recipe.defaultBiome = "meadow";
  return recipe;
}

TerrainPaletteEntry entry(std::string ruleId, std::string asset, float density,
                          float spacing) {
  TerrainPaletteEntry value;
  value.model = std::move(asset);
  value.density = density;
  value.candidateSpacing = 1;
  value.spacing = spacing;
  value.scaleMin = 1;
  value.scaleMax = 1;
  return value;
}

TerrainPalette paletteWith(TerrainPaletteEntry tree,
                           TerrainPaletteEntry grass) {
  TerrainPalette palette;
  palette.id = "asset://terrain/palettes/test";
  palette.name = "Test";
  palette.placements.emplace("tree", tree);
  palette.placements.emplace("grass", grass);
  return palette;
}

// Placement must not depend on traversal order, worker count, or repetition:
// that is the property that lets scatter stay correct alongside incremental
// editing. Every other test here is worthless if this one fails.
void resolvedWaterRejectsCandidatesBeforeSpacingAndBudget() {
  const auto recipe = flatRecipe();
  auto field = *TerrainGenerator::generate(recipe);
  for (std::size_t sample = 0; sample < field.heights.size(); ++sample) {
    field.heights.set(sample, -2.F);
  }
  TerrainPalette palette;
  palette.placements.emplace("cover", entry("cover", "asset://cover", 1, 0));
  std::vector<std::uint8_t> wet(field.heights.size(), 1);
  wet.back() = 0;
  const TerrainScatterSettings settings{.maximumPlacements = 1};
  const auto filtered =
      scatterTerrain(field, recipe, palette, settings, nullptr, wet);
  assert(filtered.placements.size() == 1);
  assert(filtered.placements.front().cell == field.heights.size() - 1);
  assert(scatterTerrain(field, recipe, palette, settings).placements.empty());
  wet.back() = 1;
  assert(scatterTerrain(field, recipe, palette, settings, nullptr, wet)
             .placements.empty());
  std::fill(wet.begin(), wet.end(), 0);
  const auto dry =
      scatterTerrain(field, recipe, palette, settings, nullptr, wet);
  assert(dry.placements.size() == 1);
  assert(dry.placements.front().position.x == 0);
  assert(dry.placements.front().position.z == 0);
  wet.pop_back();
  bool rejected = false;
  try {
    (void)scatterTerrain(field, recipe, palette, settings, nullptr, wet);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
}

void placementIsDeterministic() {
  auto recipe = flatRecipe();
  recipe.paletteId = "asset://terrain/palettes/test";
  const auto field = *TerrainGenerator::generate(recipe);
  const auto palette = paletteWith(entry("tree", "asset://t", 1, 3),
                                   entry("grass", "asset://g", 1, 0));
  const auto first = scatterTerrain(field, recipe, palette);
  for (int i = 0; i < 4; ++i) {
    const auto again = scatterTerrain(field, recipe, palette);
    assert(again.placements.size() == first.placements.size());
    for (std::size_t index = 0; index < first.placements.size(); ++index) {
      const auto &a = first.placements[index];
      const auto &b = again.placements[index];
      assert(a.ruleId == b.ruleId && a.biome == b.biome);
      assert(a.position.x == b.position.x && a.position.y == b.position.y &&
             a.position.z == b.position.z);
      assert(a.yaw == b.yaw && a.scale == b.scale);
    }
  }
  assert(!first.placements.empty());
}

// A recipe with no palette is exactly the old behaviour, and a palette handed
// to a recipe that does not name one must scatter nothing.
void namedRulesRemainIndependent() {
  const auto recipe = flatRecipe();
  const auto field = *TerrainGenerator::generate(recipe);
  TerrainPalette palette;
  palette.id = "asset://palette";
  auto sparse = entry("unused", "asset://shared_model", 1, 4);
  auto dense = sparse;
  dense.spacing = 1;
  palette.placements.emplace("pine_sparse", sparse);
  palette.placements.emplace("pine_dense", dense);
  const auto original = scatterTerrain(field, recipe, palette);
  TerrainPalette reordered;
  reordered.id = palette.id;
  reordered.placements.emplace("pine_dense", dense);
  reordered.placements.emplace("pine_sparse", sparse);
  const auto repeated = scatterTerrain(field, recipe, reordered);
  assert(original.placements.size() == repeated.placements.size());
  bool hasSparse = false, hasDense = false;
  for (std::size_t i = 0; i < original.placements.size(); ++i) {
    const auto &a = original.placements[i];
    const auto &b = repeated.placements[i];
    assert(a.ruleId == b.ruleId && a.cell == b.cell && a.yaw == b.yaw);
    hasSparse |= a.ruleId == "pine_sparse";
    hasDense |= a.ruleId == "pine_dense";
  }
  assert(hasSparse && hasDense);
}

void paletteIsOptIn() {
  const auto recipe = flatRecipe();
  const auto field = *TerrainGenerator::generate(recipe);
  const auto palette = paletteWith(entry("tree", "asset://t", 1, 2),
                                   entry("grass", "asset://g", 1, 0));
  const auto without = scatterTerrain(field, recipe, palette);
  assert(!without.placements.empty());
  // Density 0 is the documented opt-out.
  TerrainScatterSettings off;
  off.density = 0;
  assert(scatterTerrain(field, recipe, palette, off).placements.empty());
}

// Zero density disables a rule without changing other rules.
void zeroDensityNeverScatters() {
  auto recipe = flatRecipe();
  recipe.paletteId = "asset://terrain/palettes/test";
  const auto field = *TerrainGenerator::generate(recipe);
  const auto palette = paletteWith(entry("tree", "asset://t", 0, 1),
                                   entry("grass", "asset://g", 1, 0));
  const auto result = scatterTerrain(field, recipe, palette);
  for (const auto &placement : result.placements)
    assert(placement.ruleId != "tree");
  assert(!result.placements.empty());
}

// A ruleId restricted to biomes must never appear on any other biome.
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

  auto tree = entry("tree", "asset://t", 1, 2);
  tree.biomes = {"meadow"};
  auto grass = entry("grass", "asset://g", 1, 0);
  grass.biomes = {"rock"};
  const auto result = scatterTerrain(field, recipe, paletteWith(tree, grass));
  std::set<std::string> seen;
  for (const auto &placement : result.placements) {
    seen.insert(placement.biomeName);
    if (placement.ruleId == "tree")
      assert(placement.biomeName == "meadow");
    if (placement.ruleId == "grass")
      assert(placement.biomeName == "rock");
  }
  // Both roles must actually have placed, or the assertions above are vacuous.
  bool placedTree = false, placedGrass = false;
  for (const auto &placement : result.placements) {
    placedTree |= placement.ruleId == "tree";
    placedGrass |= placement.ruleId == "grass";
  }
  assert(placedTree && placedGrass);
  assert(seen.size() == 2);
  // An empty biome list means every biome, which is the documented default.
  auto anyBiome = entry("bush", "asset://b", 1, 1);
  anyBiome.biomes.clear();
  TerrainPalette palette;
  palette.placements.emplace("bush", anyBiome);
  const auto open = scatterTerrain(field, recipe, palette);
  assert(!open.placements.empty());
}

// Spacing is a real minimum distance, enforced per ruleId rather than globally.
void spacingIsEnforcedPerRole() {
  auto recipe = flatRecipe();
  recipe.paletteId = "asset://terrain/palettes/test";
  const auto field = *TerrainGenerator::generate(recipe);
  const auto palette = paletteWith(entry("tree", "asset://t", 1, 6),
                                   entry("bush", "asset://b", 1, 1));
  const auto result = scatterTerrain(field, recipe, palette);
  auto checkSpacing = [&](std::string ruleId, float minimum) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < result.placements.size(); ++i)
      for (std::size_t j = i + 1; j < result.placements.size(); ++j) {
        if (result.placements[i].ruleId != ruleId)
          continue;
        ++count;
        const auto &a = result.placements[i];
        const auto &b = result.placements[j];
        if (a.ruleId != b.ruleId)
          continue;
        const float reach = std::hypot(a.position.x - b.position.x,
                                       a.position.z - b.position.z);
        // Overlapping cells can put two centres in the same position, so the
        // guarantee is per distinct cell, not a hard global minimum.
        if (reach > 0.001F)
          assert(reach >= minimum - 0.001F);
      }
    return count;
  };
  checkSpacing("tree", 6);
  checkSpacing("bush", 1);
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
  const auto palette = paletteWith(entry("tree", "asset://t", 1, 1),
                                   entry("reed", "asset://r", 1, 0));
  const auto result = scatterTerrain(field, recipe, palette);
  assert(!result.placements.empty());
  for (const auto &placement : result.placements) {
    // Placements sit exactly on a cell's surface height, so this is the real
    // water test rather than a re-derivation.
    const int x =
        int(std::lround(placement.position.x / field.size.x * recipe.cellsX));
    const int z =
        int(std::lround(placement.position.z / field.size.y * recipe.cellsZ));
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
  auto bush = entry("bush", "asset://b", 1, 0);
  bush.scaleMin = 0.5F;
  bush.scaleMax = 2.5F;
  bush.collision = TerrainCollisionPolicy::Trigger;
  bush.prefab = "prefab://shrub";
  TerrainPalette palette;
  palette.placements.emplace("bush", bush);
  const auto result = scatterTerrain(field, recipe, palette);
  assert(!result.placements.empty());
  for (const auto &placement : result.placements) {
    assert(placement.scale >= 0.5F && placement.scale <= 2.5F);
    assert(placement.yaw >= 0.F && placement.yaw < 6.3F);
    assert(placement.collision == TerrainCollisionPolicy::Trigger);
    assert(placement.prefab == "prefab://shrub");
  }
}

// The ceiling must be honoured and reported, never silently swallowed.
void ceilingIsReported() {
  auto recipe = flatRecipe();
  recipe.paletteId = "asset://terrain/palettes/test";
  const auto field = *TerrainGenerator::generate(recipe);
  const auto palette = paletteWith(entry("grass", "asset://g", 1, 0),
                                   entry("bush", "asset://b", 1, 0));
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
  auto rockOnly = entry("tree", "asset://t", 1, 1);
  rockOnly.biomes = {"a_biome_that_does_not_exist"};
  TerrainPalette palette;
  palette.placements.emplace("tree", rockOnly);
  assert(scatterableRoles(field, recipe, palette).empty());
  const auto result = scatterTerrain(field, recipe, palette);
  assert(result.placements.empty());
  assert(!result.truncated);

  // Adding an eligible ruleId makes it scatterable again.
  palette.placements.emplace("bush", entry("bush", "asset://b", 1, 2));
  assert(scatterableRoles(field, recipe, palette).size() == 1);
}

// The generator overload must leave a recipe with no palette untouched, and
// must not scatter when handed no palette even if the recipe names one.
void generationConsumesThePalette() {
  auto recipe = flatRecipe();
  const auto bare = TerrainGenerator::generate(recipe);
  assert(bare && bare->scatterPlacements.empty() && bare->paletteId.empty());

  recipe.paletteId = "asset://terrain/palettes/test";
  auto palette = paletteWith(entry("tree", "asset://t", 1, 2),
                             entry("grass", "asset://g", 1, 0));
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
using Site = std::pair<int, int>;

std::set<Site> sitesFor(const TerrainScatterResult &result,
                        const std::string &ruleId) {
  std::set<Site> sites;
  for (const auto &placement : result.placements) {
    if (placement.ruleId == ruleId) {
      sites.emplace(placement.candidateX, placement.candidateZ);
    }
  }
  return sites;
}

void densityControlsAreIndependentAndMonotonic() {
  const auto recipe = flatRecipe();
  const auto field = *TerrainGenerator::generate(recipe);
  TerrainPalette palette;
  auto grass = entry("grass", "asset://grass", 0.4F, 0);
  grass.category = TerrainScatterCategory::Grass;
  auto tree = entry("tree", "asset://tree", 0.4F, 4);
  tree.category = TerrainScatterCategory::Tree;
  palette.placements.emplace("grass", grass);
  palette.placements.emplace("tree", tree);
  const auto baseline = scatterTerrain(field, recipe, palette);
  const auto trees = sitesFor(baseline, "tree");
  const auto grasses = sitesFor(baseline, "grass");
  assert(!trees.empty() && !grasses.empty());
  palette.grassDensity = 0.5F;
  const auto thinned = scatterTerrain(field, recipe, palette);
  assert(sitesFor(thinned, "tree") == trees);
  const auto fewerGrasses = sitesFor(thinned, "grass");
  assert(fewerGrasses.size() < grasses.size());
  assert(std::includes(grasses.begin(), grasses.end(), fewerGrasses.begin(),
                       fewerGrasses.end()));
  palette.placements.erase("grass");
  assert(sitesFor(scatterTerrain(field, recipe, palette), "tree") == trees);
}

void physicalSitesSurviveResolutionChangesAndLocalExclusions() {
  auto recipe = flatRecipe();
  auto field = *TerrainGenerator::generate(recipe);
  TerrainPalette palette;
  palette.placements.emplace("tree", entry("tree", "asset://tree", 0.8F, 4));
  palette.placements.at("tree").positionJitter = 1.F;
  const auto baseline = scatterTerrain(field, recipe, palette);
  assert(!baseline.placements.empty());
  const auto removedCell = baseline.placements.front().cell;
  field.exclusions.set(removedCell, 1.F);
  const auto edited = scatterTerrain(field, recipe, palette);
  const auto editedSites = sitesFor(edited, "tree");
  for (const auto &placement : baseline.placements) {
    const bool retained =
        editedSites.contains({placement.candidateX, placement.candidateZ});
    assert(retained == (placement.cell != removedCell));
  }
  recipe.cellsX = recipe.cellsZ = 64;
  const auto refined = *TerrainGenerator::generate(recipe);
  const auto resampled = scatterTerrain(refined, recipe, palette);
  assert(sitesFor(baseline, "tree") == sitesFor(resampled, "tree"));
  assert(baseline.placements.size() == resampled.placements.size());
  for (std::size_t index = 0; index < baseline.placements.size(); ++index) {
    const auto &before = baseline.placements[index];
    const auto &after = resampled.placements[index];
    assert(before.position.x == after.position.x &&
           before.position.z == after.position.z);
    assert(before.yaw == after.yaw && before.scale == after.scale);
  }
}

void jitterRemovesRowsAndPreservesPhysicalSpacing() {
  auto recipe = flatRecipe();
  recipe.size = {16, 16};
  recipe.cellsX = recipe.cellsZ = 8;
  const auto field = *TerrainGenerator::generate(recipe);
  TerrainPalette palette;
  auto grass = entry("grass", "asset://grass", 1, 0);
  palette.placements.emplace("grass", grass);
  const auto grid = scatterTerrain(field, recipe, palette);
  for (const auto &placement : grid.placements) {
    assert(placement.position.x == float(placement.candidateX));
    assert(placement.position.z == float(placement.candidateZ));
  }
  palette.placements.at("grass").positionJitter = 1.F;
  const auto irregular = scatterTerrain(field, recipe, palette);
  const auto repeated = scatterTerrain(field, recipe, palette);
  assert(irregular.placements.size() > 200);
  assert(irregular.placements.size() == repeated.placements.size());
  std::size_t displacedOnBothAxes = 0;
  for (std::size_t index = 0; index < irregular.placements.size(); ++index) {
    const auto &placement = irregular.placements[index];
    const auto &again = repeated.placements[index];
    const float offsetX = placement.position.x - float(placement.candidateX);
    const float offsetZ = placement.position.z - float(placement.candidateZ);
    assert(std::abs(offsetX) <= 0.5F && std::abs(offsetZ) <= 0.5F);
    assert(placement.position.x >= 0 && placement.position.x <= 16);
    assert(placement.position.z >= 0 && placement.position.z <= 16);
    assert(placement.position.x == again.position.x);
    assert(placement.position.z == again.position.z);
    assert(placement.yaw == again.yaw && placement.scale == again.scale);
    if (std::abs(offsetX) > 0.05F && std::abs(offsetZ) > 0.05F) {
      ++displacedOnBothAxes;
    }
  }
  assert(displacedOnBothAxes > irregular.placements.size() / 2);
  // Independent variation streams survive changing only distribution.
  for (const auto &placement : irregular.placements) {
    const auto sameSite =
        std::ranges::find_if(grid.placements, [&](const auto &other) {
          return other.candidateX == placement.candidateX &&
                 other.candidateZ == placement.candidateZ;
        });
    assert(sameSite != grid.placements.end());
    assert(sameSite->yaw == placement.yaw &&
           sameSite->scale == placement.scale);
  }
  palette.placements.emplace("a_unrelated",
                             entry("a_unrelated", "asset://rock", 0.2F, 0));
  palette.placements.at("grass").density = 0.5F;
  const auto thinned = scatterTerrain(field, recipe, palette);
  for (const auto &placement : thinned.placements) {
    if (placement.ruleId != "grass") {
      continue;
    }
    const auto survivor =
        std::ranges::find_if(irregular.placements, [&](const auto &other) {
          return other.candidateX == placement.candidateX &&
                 other.candidateZ == placement.candidateZ;
        });
    assert(survivor != irregular.placements.end());
    assert(survivor->position.x == placement.position.x);
    assert(survivor->position.z == placement.position.z);
    assert(survivor->yaw == placement.yaw &&
           survivor->scale == placement.scale);
  }
  palette.placements.erase("a_unrelated");
  palette.placements.at("grass").density = 1.F;
  palette.placements.at("grass").spacing = 1.5F;
  const auto separated = scatterTerrain(field, recipe, palette);
  assert(!separated.placements.empty());
  for (std::size_t first = 0; first < separated.placements.size(); ++first) {
    for (std::size_t second = first + 1; second < separated.placements.size();
         ++second) {
      const auto &a = separated.placements[first].position;
      const auto &b = separated.placements[second].position;
      assert(std::hypot(a.x - b.x, a.z - b.z) >= 1.5F);
    }
  }
}

void globalCandidateWindowsMatchWholeField() {
  auto recipe = flatRecipe();
  recipe.cellsX = recipe.cellsZ = 64;
  auto whole = *TerrainGenerator::generate(recipe);
  TerrainMasks masks;
  masks.slope.resize(whole.heights.size());
  masks.moisture.resize(whole.heights.size());
  masks.waterDistance.resize(whole.heights.size());
  std::vector<std::uint8_t> wet(whole.heights.size());
  for (int z = 0; z <= 64; ++z) {
    for (int x = 0; x <= 64; ++x) {
      const auto index = whole.index(x, z);
      const float distance =
          std::max(0.F, std::hypot(float(x - 30), float(z - 30)) - 5.F);
      masks.slope.set(index, 8.F + 4.F * std::sin(float(x) * .1F));
      masks.moisture.set(index, .5F + .2F * std::cos(float(z) * .1F));
      masks.waterDistance.set(index, distance);
      wet[index] = distance == 0;
      whole.exclusions.set(index, x > 47 && z < 20 ? 1.F : 0.F);
    }
  }
  TerrainPalette palette;
  palette.id = "asset://test/shared-domain";
  auto plants = entry("plants", "asset://plants", .6F, 0);
  plants.clusterSize = 12;
  plants.clusterStrength = .45F;
  plants.moisture = {.3F, .8F};
  plants.waterDistance = {2, 1000};
  auto trees = entry("trees", "asset://trees", .12F, 3);
  trees.candidateSpacing = 2;
  trees.positionJitter = 1;
  trees.waterDistance = {4, 1000};
  palette.placements.emplace("plants", plants);
  palette.placements.emplace("trees", trees);
  const TerrainScatterWindow worldBounds{1000, -2000, 1064, -1936};
  const auto constraintsFor = [&](const HeightField &field,
                                  const TerrainMasks &localMasks, float firstX,
                                  float firstZ) {
    auto localRecipe = recipe;
    localRecipe.size = field.size;
    localRecipe.cellsX = field.cellsX;
    localRecipe.cellsZ = field.cellsZ;
    auto constraints = TerrainScatterConstraints::build(
        field, localRecipe, &localMasks,
        {.seaLevel = std::numeric_limits<float>::lowest(), .authored = true});
    constraints.addProtectedArea({40 - firstX, 40 - firstZ}, 4);
    constraints.addRoad(
        {{5 - firstX, 0, 50 - firstZ}, {58 - firstX, 0, 12 - firstZ}}, 2);
    return constraints;
  };
  const auto wholeConstraints = constraintsFor(whole, masks, 0, 0);
  for (const float jitter : {0.F, 1.F}) {
    palette.placements.at("plants").positionJitter = jitter;
    TerrainScatterSettings settings;
    settings.domain =
        TerrainScatterDomain{worldBounds, {1000, -2000}, worldBounds};
    const auto baseline = scatterTerrain(whole, recipe, palette, settings,
                                         &wholeConstraints, wet);
    assert(!baseline.placements.empty());
    std::map<std::string, TerrainScatterPlacement> expected;
    for (const auto &placement : baseline.placements) {
      expected.emplace(terrainScatterInstanceKey(palette.id, placement.ruleId,
                                                 placement.candidateX,
                                                 placement.candidateZ),
                       placement);
    }
    // Reverse load order as well as compare two passes. Every field has a 2m
    // halo where available, while ownership remains half-open and
    // nonoverlapping.
    for (const bool reverse : {false, true}) {
      std::map<std::string, TerrainScatterPlacement> merged;
      for (int visit = 0; visit < 4; ++visit) {
        const int quadrant = reverse ? 3 - visit : visit;
        const int ownX = (quadrant % 2) * 32;
        const int ownZ = (quadrant / 2) * 32;
        const int firstX = std::max(0, ownX - 2);
        const int firstZ = std::max(0, ownZ - 2);
        const int lastX = std::min(64, ownX + 34);
        const int lastZ = std::min(64, ownZ + 34);
        HeightField tile;
        tile.cellsX = lastX - firstX;
        tile.cellsZ = lastZ - firstZ;
        tile.size = {float(tile.cellsX), float(tile.cellsZ)};
        tile.biomeIds = whole.biomeIds;
        const auto count = std::size_t(tile.cellsX + 1) * (tile.cellsZ + 1);
        tile.heights.resize(count);
        tile.biomeIndices.resize(count);
        tile.exclusions.resize(count);
        TerrainMasks localMasks;
        localMasks.slope.resize(count);
        localMasks.moisture.resize(count);
        localMasks.waterDistance.resize(count);
        std::vector<std::uint8_t> localWet(count);
        for (int z = 0; z <= tile.cellsZ; ++z) {
          for (int x = 0; x <= tile.cellsX; ++x) {
            const auto from = whole.index(x + firstX, z + firstZ);
            const auto to = tile.index(x, z);
            tile.heights.set(to, whole.heights[from]);
            tile.biomeIndices.set(to, whole.biomeIndices[from]);
            tile.exclusions.set(to, whole.exclusions[from]);
            localMasks.slope.set(to, masks.slope[from]);
            localMasks.moisture.set(to, masks.moisture[from]);
            localMasks.waterDistance.set(to, masks.waterDistance[from]);
            localWet[to] = wet[from];
          }
        }
        const auto constraints =
            constraintsFor(tile, localMasks, float(firstX), float(firstZ));
        settings.domain = TerrainScatterDomain{
            worldBounds,
            {1000.0 + firstX, -2000.0 + firstZ},
            {1000.0 + ownX, -2000.0 + ownZ, 1032.0 + ownX, -1968.0 + ownZ}};
        const auto result = scatterTerrain(tile, recipe, palette, settings,
                                           &constraints, localWet);
        for (auto placement : result.placements) {
          const auto id = terrainScatterInstanceKey(
              palette.id, placement.ruleId, placement.candidateX,
              placement.candidateZ);
          const auto &original = expected.at(id);
          const double globalX =
              settings.domain->fieldOrigin.x + placement.position.x;
          const double globalZ =
              settings.domain->fieldOrigin.z + placement.position.z;
          assert(globalX == worldBounds.minX + original.position.x);
          assert(globalZ == worldBounds.minZ + original.position.z);
          assert(std::bit_cast<std::uint32_t>(placement.position.y) ==
                 std::bit_cast<std::uint32_t>(original.position.y));
          assert(placement.yaw == original.yaw &&
                 placement.scale == original.scale);
          assert(placement.biomeName == original.biomeName);
          placement.position.x += float(firstX);
          placement.position.z += float(firstZ);
          assert(merged.emplace(id, std::move(placement)).second);
        }
      }
      assert(merged.size() == expected.size());
      for (auto first = merged.begin(); first != merged.end(); ++first) {
        if (first->second.ruleId != "trees") {
          continue;
        }
        for (auto second = std::next(first); second != merged.end(); ++second) {
          if (second->second.ruleId == "trees") {
            const auto &a = first->second.position;
            const auto &b = second->second.position;
            assert(std::hypot(a.x - b.x, a.z - b.z) >= 3.F);
          }
        }
      }
    }
    settings.domain =
        TerrainScatterDomain{worldBounds, {1000, -2000}, worldBounds};
    bool rejected = false;
    try {
      (void)scatterTerrain(whole, recipe, palette, settings, nullptr, wet);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
    rejected = false;
    try {
      (void)scatterTerrain(whole, recipe, palette, settings, &wholeConstraints);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
    // Work is bounded by ownership, not the total procedural world area.
    settings.domain->worldBounds.maxX = 1001000;
    settings.domain->worldBounds.maxZ = 998000;
    settings.maximumCandidates = 20000;
    assert(!scatterTerrain(whole, recipe, palette, settings, &wholeConstraints,
                           wet)
                .placements.empty());
  }
}

void denseGrassDoesNotDependOnTerrainSamples() {
  auto recipe = flatRecipe();
  recipe.size = {8, 8};
  recipe.cellsX = recipe.cellsZ = 2;
  const auto field = *TerrainGenerator::generate(recipe);
  TerrainPalette palette;
  auto grass = entry("grass", "asset://grass", 8, 0);
  grass.candidateSpacing = 0.25F;
  palette.placements.emplace("grass", grass);
  const auto result = scatterTerrain(field, recipe, palette);
  assert(result.placements.size() > field.heights.size() * 20);
  grass.density = 17;
  palette.placements.at("grass") = grass;
  bool rejected = false;
  try {
    (void)scatterTerrain(field, recipe, palette);
  } catch (const std::invalid_argument &failure) {
    rejected = std::string(failure.what()).find("candidate capacity") !=
               std::string::npos;
  }
  assert(rejected);
}

void rulesUseSharedEnvironmentAndSpatialConstraints() {
  const auto recipe = flatRecipe();
  const auto field = *TerrainGenerator::generate(recipe);
  TerrainMasks masks;
  masks.slope.assign(field.heights.size(), 55.F);
  masks.moisture.assign(field.heights.size(), 0.4F);
  masks.waterDistance.assign(field.heights.size(), 8.F);
  auto constraints =
      TerrainScatterConstraints::build(field, recipe, &masks, {});
  TerrainPalette palette;
  auto rock = entry("rock", "asset://rock", 1, 0);
  rock.slope = {50, 60};
  rock.moisture = {0.3F, 0.5F};
  rock.waterDistance = {7, 9};
  palette.placements.emplace("rock", rock);
  assert(!scatterTerrain(field, recipe, palette, {}, &constraints)
              .placements.empty());
  palette.placements.at("rock").moisture = {0.6F, 1};
  assert(scatterTerrain(field, recipe, palette, {}, &constraints)
             .placements.empty());
  assert(constraints.rejectionReason(0, palette.placements.at("rock")) ==
         "moisture");
  palette.placements.at("rock") = rock;
  constraints.addProtectedArea({32, 32}, 12);
  constraints.addRoad({{0, 0, 12}, {64, 0, 12}}, 3);
  constraints.addFootprint({{2, 2}, {10, 2}, {10, 10}, {2, 10}});
  const auto scattered =
      scatterTerrain(field, recipe, palette, {}, &constraints);
  assert(!scattered.placements.empty());
  for (const auto &placement : scattered.placements) {
    assert(
        constraints.allows(placement.cell, rock,
                           Vec2{placement.position.x, placement.position.z}));
  }
}

void clustersAndCancellationKeepStableSites() {
  const auto recipe = flatRecipe();
  const auto field = *TerrainGenerator::generate(recipe);
  TerrainPalette palette;
  auto grass = entry("grass", "asset://grass", 0.8F, 0);
  palette.placements.emplace("grass", grass);
  const auto uniform =
      sitesFor(scatterTerrain(field, recipe, palette), "grass");
  palette.placements.at("grass").clusterStrength = 1;
  const auto clustered =
      sitesFor(scatterTerrain(field, recipe, palette), "grass");
  assert(!clustered.empty() && clustered.size() < uniform.size());
  assert(std::includes(uniform.begin(), uniform.end(), clustered.begin(),
                       clustered.end()));
  std::stop_source cancellation;
  cancellation.request_stop();
  assert(scatterTerrain(field, recipe, palette, {}, nullptr, {},
                        cancellation.get_token())
             .placements.empty());
  bool bounded = false;
  try {
    (void)scatterTerrain(field, recipe, palette, {.maximumCandidates = 4});
  } catch (const std::invalid_argument &failure) {
    bounded =
        std::string(failure.what()).find("work budget") != std::string::npos;
  }
  assert(bounded);
}

void finalSurfaceRefilterRechecksRulesAndRestoresCandidates() {
  auto recipe = flatRecipe();
  auto field = *TerrainGenerator::generate(recipe);
  field.heights.assign(field.heights.size(), 8.F);
  TerrainPalette palette;
  auto cover = entry("cover", "asset://cover", 1, 0);
  cover.biomes = {"meadow"};
  cover.slope = {0, 20};
  palette.placements.emplace("cover", cover);
  const auto candidates = scatterTerrain(field, recipe, palette).placements;
  assert(!candidates.empty());
  const auto baselineSize = candidates.size();
  const auto cell = candidates.front().cell;
  const auto oldBiome = field.biomeIndices[cell];
  const auto rock =
      std::find(field.biomeIds.begin(), field.biomeIds.end(), "rock");
  assert(rock != field.biomeIds.end());
  field.biomeIndices.set(cell, std::size_t(rock - field.biomeIds.begin()));
  assert(refreshTerrainScatterPlacements(field, candidates, recipe, &palette));
  assert(field.scatterPlacements.size() < baselineSize);
  field.biomeIndices.set(cell, oldBiome);
  assert(refreshTerrainScatterPlacements(field, candidates, recipe, &palette));
  assert(field.scatterPlacements.size() == baselineSize);

  for (int z = 0; z <= field.cellsZ; ++z) {
    for (int x = 0; x <= field.cellsX; ++x) {
      field.heights.set(field.index(x, z), 8.F + float(x) * 2.F);
    }
  }
  assert(refreshTerrainScatterPlacements(field, candidates, recipe, &palette));
  assert(field.scatterPlacements.empty());
  field.heights.assign(field.heights.size(), 8.F);
  palette.placements.at("cover").moisture = {0, 0};
  assert(refreshTerrainScatterPlacements(field, candidates, recipe, &palette));
  assert(field.scatterPlacements.empty());
  palette.placements.at("cover").moisture = {0, 1};
  assert(refreshTerrainScatterPlacements(field, candidates, recipe, &palette));
  assert(field.scatterPlacements.size() == baselineSize);

  palette.placements.at("cover").waterDistance = {0, 4};
  assert(refreshTerrainScatterPlacements(field, candidates, recipe, &palette));
  assert(field.scatterPlacements.empty());
  TerrainWaterAuthoring water;
  water.authored = true;
  water.bodies.push_back({.id = "pond",
                          .kind = TerrainWaterBody::Lake,
                          .level = 9,
                          .center = {32, 32},
                          .radius = 10});
  assert(refreshTerrainScatterPlacements(field, candidates, recipe, &palette,
                                         &water));
  assert(!field.scatterPlacements.empty() &&
         field.scatterPlacements.size() < baselineSize);
  palette.placements.at("cover").waterDistance = {};
  field.heights.assign(field.heights.size(), -2.F);
  water.bodies.clear();
  assert(refreshTerrainScatterPlacements(field, candidates, recipe, &palette,
                                         &water));
  assert(field.scatterPlacements.size() == baselineSize);
  assert(refreshTerrainScatterPlacements(field, candidates, recipe, &palette));
  assert(field.scatterPlacements.empty());
  field.heights.assign(field.heights.size(), 8.F);

  TerrainEdit protection;
  protection.kind = TerrainEditKind::Protect;
  protection.center = {32, 32};
  protection.radius = 8;
  protection.snapshotSize = field.size;
  protection.snapshotCellsX = field.cellsX;
  protection.snapshotCellsZ = field.cellsZ;
  protection.samples.push_back({{32, 32}, 8});
  recipe.edits.push_back(protection);
  assert(refreshTerrainScatterPlacements(field, candidates, recipe, &palette));
  assert(!field.scatterPlacements.empty() &&
         field.scatterPlacements.size() < baselineSize);
  for (const auto &placement : field.scatterPlacements) {
    assert(std::hypot(placement.position.x - 32.F,
                      placement.position.z - 32.F) > 8.F);
  }
  recipe.edits.clear();
  assert(refreshTerrainScatterPlacements(field, candidates, recipe, &palette));
  assert(field.scatterPlacements.size() == baselineSize);
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    assert(field.scatterPlacements[index].candidateX ==
           candidates[index].candidateX);
    assert(field.scatterPlacements[index].candidateZ ==
           candidates[index].candidateZ);
    assert(field.scatterPlacements[index].yaw == candidates[index].yaw);
  }
}

} // namespace

int main() {
  finalSurfaceRefilterRechecksRulesAndRestoresCandidates();
  densityControlsAreIndependentAndMonotonic();
  physicalSitesSurviveResolutionChangesAndLocalExclusions();
  jitterRemovesRowsAndPreservesPhysicalSpacing();
  globalCandidateWindowsMatchWholeField();
  denseGrassDoesNotDependOnTerrainSamples();
  rulesUseSharedEnvironmentAndSpatialConstraints();
  clustersAndCancellationKeepStableSites();
  placementIsDeterministic();
  resolvedWaterRejectsCandidatesBeforeSpacingAndBudget();
  namedRulesRemainIndependent();
  paletteIsOptIn();
  zeroDensityNeverScatters();
  rolesRespectBiomeFilter();
  spacingIsEnforcedPerRole();
  waterAndExclusionsAreRespected();
  scaleAndRotationAreInRange();
  ceilingIsReported();
  scatterableRolesAreReported();
  generationConsumesThePalette();
  std::cout << "Terrain scatter checks passed\n";
}
