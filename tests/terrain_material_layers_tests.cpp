#include "demi/runtime/terrain/TerrainMaterialLayers.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace demi::runtime;
using demi::assets::TerrainMaterialRole;

namespace {

// A 64x64 world on an 8-cell grid: nine samples per axis at 0, 8, ... 64. Small
// enough that every assertion names a cell by its grid coordinate and reads as
// a claim about a surface rather than about index arithmetic.
constexpr float WorldSize = 64.F;
constexpr int Cells = 8;

std::size_t sampleCount() {
  return std::size_t(Cells + 1) * std::size_t(Cells + 1);
}

struct Scene {
  HeightField field;
  TerrainMasks masks;
  TerrainMaterialLayerSettings settings;
};

// Every role bound, so a cell that wants a role is never quietly short of one.
// The ids are distinct strings because a test that compared two roles could
// otherwise pass on a material id it never meant to check.
demi::assets::TerrainMaterialSet fullSet() {
  demi::assets::TerrainMaterialSet set;
  set.id = "asset://materials/terrain/full";
  set.name = "full";
  for (const char *role :
       {"rock", "cliff", "sediment", "ground", "grass", "sand", "snow",
        "wet_ground", "underwater"})
    set.roles.emplace(role, std::string("asset://materials/terrain/") + role);
  set.requiredRoles = {"ground", "rock"};
  return set;
}

demi::assets::TerrainMaterialSet setWithout(const char *roleName) {
  auto set = fullSet();
  set.roles.erase(roleName);
  return set;
}

// Dry, flat, unexcluded ground ten units above a water level of zero, far from
// any water, with a modest moisture. Every surface under test has to be
// produced by the one mask it names, so the baseline sets nothing that any of
// them reads. `scale` multiplies the world size, the heights and the water
// distance together, which is how the scale-independence claim is tested: a
// terrain authored ten times larger is the same terrain.
Scene plainScene(float scale = 1.F) {
  Scene scene;
  scene.field.size = {WorldSize * scale, WorldSize * scale};
  scene.field.cellsX = scene.field.cellsZ = Cells;
  scene.field.baseHeights.assign(sampleCount(), 10.F * scale);
  scene.field.heights = scene.field.baseHeights;
  scene.field.normals.assign(sampleCount(), Vec3{0, 1, 0});
  scene.field.biomeIndices.assign(sampleCount(), std::size_t{0});
  scene.field.exclusions.assign(sampleCount(), 0.F);
  scene.field.biomeIds = {"default"};
  scene.field.biomeColors = {Color{}};
  scene.masks.slope.assign(sampleCount(), 0.F);
  scene.masks.moisture.assign(sampleCount(), 0.2F);
  scene.masks.waterDistance.assign(sampleCount(), 1000.F * scale);
  scene.masks.flow.assign(sampleCount(), 0.F);
  scene.masks.sediment.assign(sampleCount(), 0.F);
  scene.masks.substrate.assign(sampleCount(), std::size_t{0}); // Soil
  scene.settings.waterLevel = 0.F;
  // The wet band is an authored distance in world units, so it is part of what
  // scaling the terrain scales: a shore two units wide stays two units wide.
  scene.settings.shoreDistance = 2.F * scale;
  return scene;
}

// The same plain with a high plateau across its far edge: a quarter of the
// samples stand above the rest, which is what gives the field real relief and a
// snow line derived from its own heights to sit inside.
Scene mountainScene(float scale = 1.F) {
  auto scene = plainScene(scale);
  for (int z = 0; z <= Cells; ++z)
    for (int x = 0; x <= Cells; ++x)
      if (z >= Cells - 1)
        scene.field.heights.set(scene.field.index(x, z), 40.F * scale);
  return scene;
}

// The sample at this cell coordinate. Tests name a position on the grid rather
// than a world distance, so the same cell is the same cell on a terrain
// authored at a different scale.
std::size_t cellAt(const Scene &scene, int x, int z) {
  return scene.field.index(x, z);
}

// A damp, flat cell inside the wet band, on the derived wet substrate. Every
// surface role but WetGround either declines here or scores lower, so this is
// the claim that wet ground is reachable at all. The distance is in the same
// world units as the terrain it sits on, so two scenes at different scales can
// be compared.
void makeWetCell(Scene &scene, int x, int z, float scale = 1.F) {
  const auto cell = cellAt(scene, x, z);
  scene.masks.moisture.set(cell, 0.9F);
  scene.masks.waterDistance.set(cell, 0.5F * scale);
  scene.masks.substrate.set(cell, std::size_t{3}); // Wet
}

std::string roleAt(const Scene &scene,
                   const demi::assets::TerrainMaterialSet &set,
                   std::size_t cell,
                   const TerrainMaterialLayerSettings *settings = nullptr) {
  const auto decision = decideTerrainMaterial(
      set, scene.field, scene.masks, cell,
      settings != nullptr ? *settings : scene.settings);
  return decision.roleName;
}

bool listed(const std::vector<std::string> &names, const char *wanted) {
  return std::find(names.begin(), names.end(), wanted) != names.end();
}

// The baseline every other assertion depends on: dry open ground away from
// water is bare soil, not something every role claims.
void dryOpenGroundIsGround() {
  const auto scene = plainScene();
  const auto set = fullSet();
  const auto decisions =
      decideTerrainMaterials(set, scene.field, scene.masks, scene.settings);
  assert(decisions.size() == sampleCount());
  for (const auto &decision : decisions) {
    assert(decision.roleName == "ground");
    assert(decision.materialAssetId == "asset://materials/terrain/ground");
    assert(decision.score > 0.F);
  }
}

// A steep cell is exposed ground. The ramp reaches its ceiling only well up the
// face, so this claims the angle threshold rather than "anything above zero".
void steepGroundIsRock() {
  auto scene = plainScene();
  const auto set = fullSet();
  const auto cell = cellAt(scene, 4, 4);
  scene.masks.slope.set(cell, 45.F);
  const auto decision = decideTerrainMaterial(set, scene.field, scene.masks,
                                              cell, scene.settings);
  assert(decision.roleName == "rock");
  assert(decision.materialAssetId == "asset://materials/terrain/rock");
  // Halfway up the 32..58 degree ramp.
  assert(decision.score > 0.44F && decision.score < 0.46F);

  // The face material takes the sample over once it is genuinely near-vertical,
  // because its ramp reaches a higher ceiling than rock's.
  scene.masks.slope.set(cell, 72.F);
  assert(roleAt(scene, set, cell) == "cliff");
}

// Wet ground is the shoreline inside the wet band, and it has to be reachable:
// grass, soil and sand all decline or score lower on a damp flat cell.
void flatWetCellIsWetGround() {
  auto scene = plainScene();
  const auto set = fullSet();
  makeWetCell(scene, 4, 4);
  const auto cell = cellAt(scene, 4, 4);
  const auto decision = decideTerrainMaterial(set, scene.field, scene.masks,
                                              cell, scene.settings);
  assert(decision.roleName == "wet_ground");
  assert(decision.materialAssetId == "asset://materials/terrain/wet_ground");
  assert(decision.score > 0.F);
}

// A cell far from water is not wet however damp its moisture mask is: the wet
// band is a distance in world units, and moisture is not a substitute for it.
void farFromWaterIsNotWet() {
  auto scene = plainScene();
  const auto set = fullSet();
  const auto cell = cellAt(scene, 4, 4);
  scene.masks.moisture.set(cell, 1.F);
  scene.masks.waterDistance.set(cell, 400.F);
  assert(roleAt(scene, set, cell) == "grass");
  // Outside the beach band as well, so the shore roles cannot claim it either,
  // and only damp enough to stay below the grass crossover.
  scene.masks.moisture.set(cell, 0.6F);
  assert(roleAt(scene, set, cell) == "ground");
}

// A submerged bed and a merely damp shore are two roles, not two shades of wet
// ground, and the score says which is which.
void submergedDiffersFromDamp() {
  auto scene = plainScene();
  const auto set = fullSet();
  const auto damp = cellAt(scene, 2, 2);
  makeWetCell(scene, 2, 2);
  const auto lake = cellAt(scene, 6, 6);
  scene.field.heights.set(lake, -3.F);
  scene.masks.waterDistance.set(lake, 0.F);
  scene.masks.moisture.set(lake, 0.9F);
  scene.masks.substrate.set(lake, std::size_t{3});

  const auto bed =
      decideTerrainMaterial(set, scene.field, scene.masks, lake,
                            scene.settings);
  const auto shore =
      decideTerrainMaterial(set, scene.field, scene.masks, damp,
                            scene.settings);
  assert(bed.roleName == "underwater");
  assert(shore.roleName == "wet_ground");
  assert(bed.roleName != shore.roleName);
  assert(bed.materialAssetId == "asset://materials/terrain/underwater");
  assert(bed.score > 0.F);
  // The depth term is what separates them numerically: the bed is a deeper
  // water sample than the shore is.
  assert(bed.score != shore.score);

  // A submerged cliff is still a submerged bed: no surface role competes for
  // it, so the water never ends in a fringe of dry rock.
  scene.masks.slope.set(lake, 60.F);
  assert(roleAt(scene, set, lake) == "underwater");

  // A shoreline deep enough to be under water stops being a shore.
  scene.field.heights.set(damp, -0.5F);
  assert(roleAt(scene, set, damp) == "underwater");
}

// The dry part of the same shore is sand, and it is a separate role from the
// wet part: the beach band is the region past the wet band, not a name for
// both.
void dryBeachIsSand() {
  auto scene = plainScene();
  const auto set = fullSet();
  const auto cell = cellAt(scene, 4, 4);
  scene.masks.moisture.set(cell, 0.1F);
  scene.masks.waterDistance.set(cell, 3.F);
  scene.masks.substrate.set(cell, std::size_t{2}); // Sand
  assert(roleAt(scene, set, cell) == "sand");

  // Past the beach band it is ordinary soil again, so the band is a reach
  // rather than a permanent "near water" flag.
  scene.masks.waterDistance.set(cell, 60.F);
  assert(roleAt(scene, set, cell) == "ground");
}

// Snow follows the field's own relief: the plateau above the derived line wears
// it, the plain below does not.
void highFlatGroundIsSnow() {
  const auto scene = mountainScene();
  const auto set = fullSet();
  const auto ridge = cellAt(scene, 4, 7);
  const auto plain = cellAt(scene, 4, 2);
  assert(scene.field.heights[ridge] > scene.field.heights[plain]);
  const auto decision = decideTerrainMaterial(set, scene.field, scene.masks,
                                              ridge, scene.settings);
  assert(decision.roleName == "snow");
  assert(decision.materialAssetId == "asset://materials/terrain/snow");
  assert(decision.score > 0.9F);
  assert(roleAt(scene, set, plain) == "ground");
}

// Snow does not stick to a cliff: the same high cell, steep, is rock.
void highSteepGroundIsNotSnow() {
  auto scene = mountainScene();
  const auto set = fullSet();
  const auto ridge = cellAt(scene, 4, 7);
  scene.masks.slope.set(ridge, 60.F);
  assert(roleAt(scene, set, ridge) == "rock");

  // Just inside the limit the snow line still wins the altitude, which is what
  // makes the limit a limit rather than a cliff test.
  scene.masks.slope.set(ridge, 30.F);
  assert(roleAt(scene, set, ridge) == "snow");
}

// Turning the snow line off is not losing an argument to a higher score: no
// altitude reaches snow at all.
void allowSnowLineCanBeDisabled() {
  auto scene = mountainScene();
  const auto set = fullSet();
  auto settings = scene.settings;
  settings.allowSnowLine = false;
  for (std::size_t cell = 0; cell < sampleCount(); ++cell)
    assert(roleAt(scene, set, cell, &settings) != "snow");
  // The line is otherwise still in force, so this is the setting and not a
  // field that happens to have no snow.
  assert(roleAt(scene, set, cellAt(scene, 4, 7)) == "snow");
}

// An authored snow line wins over the derived one, and an unauthored field with
// no dry relief has no snow line rather than one at height zero.
void snowLineIsDerivedOrAuthored() {
  const auto scene = mountainScene();
  const auto set = fullSet();
  auto raised = scene.settings;
  raised.snowLine = 30.F;
  assert(roleAt(scene, set, cellAt(scene, 4, 7), &raised) == "snow");
  raised.snowLine = 45.F;
  assert(roleAt(scene, set, cellAt(scene, 4, 7), &raised) == "ground");

  // A plain has no dry relief at all: every sample is the same height, so there
  // is nowhere for a line to sit and nothing is snow.
  const auto plain = plainScene();
  for (std::size_t cell = 0; cell < sampleCount(); ++cell)
    assert(roleAt(plain, set, cell) != "snow");
  auto atZero = plain.settings;
  atZero.snowLine = 0.F;
  assert(roleAt(plain, set, 0, &atZero) != "snow");
}

// Nothing in a decision may depend on the size of the world: the same masks on
// a terrain authored ten times larger, with heights and water distances scaled
// with it, are the same surfaces.
void decisionsDoNotDependOnWorldScale() {
  const auto set = fullSet();
  const auto small = mountainScene(1.F);
  const auto large = mountainScene(10.F);
  auto wetSmall = plainScene(1.F);
  auto wetLarge = plainScene(10.F);
  makeWetCell(wetSmall, 4, 4, 1.F);
  makeWetCell(wetLarge, 4, 4, 10.F);
  const auto drySmall =
      decideTerrainMaterials(set, small.field, small.masks, small.settings);
  const auto dryLarge =
      decideTerrainMaterials(set, large.field, large.masks, large.settings);
  assert(drySmall.size() == dryLarge.size());
  for (std::size_t cell = 0; cell < drySmall.size(); ++cell) {
    assert(drySmall[cell].roleName == dryLarge[cell].roleName);
    assert(drySmall[cell].materialAssetId == dryLarge[cell].materialAssetId);
  }
  assert(drySmall[cellAt(small, 4, 7)].score ==
         dryLarge[cellAt(large, 4, 7)].score);

  // The wet cell keeps its role, and its score is computed in the same world
  // units: a band two units wide stays two units wide on a ten-times-larger
  // terrain.
  const auto wetCellSmall = cellAt(wetSmall, 4, 4);
  const auto wetCellLarge = cellAt(wetLarge, 4, 4);
  const auto dampSmall =
      decideTerrainMaterial(set, wetSmall.field, wetSmall.masks, wetCellSmall,
                            wetSmall.settings);
  const auto dampLarge =
      decideTerrainMaterial(set, wetLarge.field, wetLarge.masks, wetCellLarge,
                            wetLarge.settings);
  assert(dampSmall.roleName == "wet_ground");
  assert(dampLarge.roleName == "wet_ground");
  assert(dampSmall.score == dampLarge.score);
}

// The decision is a pure function of its inputs: two resolves of the same field
// agree cell for cell, including the scores.
void decisionsAreReproducible() {
  const auto set = fullSet();
  const auto first = mountainScene();
  const auto second = mountainScene();
  const auto one =
      decideTerrainMaterials(set, first.field, first.masks, first.settings);
  const auto two =
      decideTerrainMaterials(set, second.field, second.masks, second.settings);
  assert(one.size() == two.size());
  for (std::size_t cell = 0; cell < one.size(); ++cell) {
    assert(one[cell].role == two[cell].role);
    assert(one[cell].roleName == two[cell].roleName);
    assert(one[cell].materialAssetId == two[cell].materialAssetId);
    assert(one[cell].score == two[cell].score);
  }
  // Resolving the same field twice in one process agrees as well, so nothing is
  // accumulating between calls.
  const auto again =
      decideTerrainMaterials(set, first.field, first.masks, first.settings);
  for (std::size_t cell = 0; cell < again.size(); ++cell) {
    assert(again[cell].role == one[cell].role);
    assert(again[cell].score == one[cell].score);
  }
}

// Asking about one cell before another cannot change either answer. This is the
// property an editor's hover and a background full resolve both depend on.
void queryOrderDoesNotChangeDecisions() {
  auto scene = mountainScene();
  const auto set = fullSet();
  makeWetCell(scene, 4, 4);
  scene.masks.slope.set(cellAt(scene, 1, 1), 45.F);
  scene.masks.slope.set(cellAt(scene, 5, 1), 72.F);
  scene.masks.flow.set(cellAt(scene, 1, 7), 1.F);
  scene.masks.sediment.set(cellAt(scene, 3, 5), 2.F);
  const auto whole =
      decideTerrainMaterials(set, scene.field, scene.masks, scene.settings);

  // The steep, wet and snowy cells first, then a coarse backward walk, so the
  // order is not the field's own and not the reverse of it either.
  const std::size_t leading[] = {
      cellAt(scene, 5, 1), cellAt(scene, 4, 4), cellAt(scene, 4, 7),
      cellAt(scene, 1, 1), cellAt(scene, 1, 7), cellAt(scene, 3, 5)};
  for (const std::size_t cell : leading) {
    const auto single = decideTerrainMaterial(set, scene.field, scene.masks,
                                              cell, scene.settings);
    assert(single.role == whole[cell].role);
    assert(single.roleName == whole[cell].roleName);
    assert(single.materialAssetId == whole[cell].materialAssetId);
    assert(single.score == whole[cell].score);
  }
  for (auto cell = sampleCount(); cell-- > 0;) {
    const auto single = decideTerrainMaterial(set, scene.field, scene.masks,
                                              cell, scene.settings);
    assert(single.role == whole[cell].role);
    assert(single.score == whole[cell].score);
  }
  // The scene really did exercise several roles, or the walk above would be
  // comparing one answer with itself.
  std::vector<std::string> distinct;
  for (const auto &decision : whole) {
    if (decision.roleName.empty() ||
        listed(distinct, decision.roleName.c_str()))
      continue;
    distinct.push_back(decision.roleName);
  }
  assert(distinct.size() >= 6);
}

// A role the field wants and the set does not bind is reported, not borrowed
// from another role: the cells that want it carry no material at all.
void unboundRoleIsReported() {
  const auto scene = mountainScene();
  const auto withoutSnow = setWithout("snow");
  const auto decisions = decideTerrainMaterials(
      withoutSnow, scene.field, scene.masks, scene.settings);
  std::size_t snowy = 0;
  for (const auto &decision : decisions) {
    if (decision.roleName != "snow")
      continue;
    ++snowy;
    assert(decision.materialAssetId.empty());
  }
  assert(snowy > 0);

  const auto coverage =
      terrainMaterialCoverage(withoutSnow, scene.field, decisions);
  assert(listed(coverage.unbound, "snow"));
  assert(!coverage.unbound.empty());
  assert(coverage.unassigned == snowy);
  assert(coverage.assigned == decisions.size() - snowy);
  assert(coverage.assigned + coverage.unassigned == decisions.size());
  // The set's own roles are all reachable here, so nothing else is missing.
  assert(!listed(coverage.unbound, "rock"));

  // With the role bound, the same field has no gaps at all.
  const auto set = fullSet();
  const auto complete = decideTerrainMaterials(set, scene.field, scene.masks,
                                               scene.settings);
  const auto full = terrainMaterialCoverage(set, scene.field, complete);
  assert(full.unbound.empty());
  assert(full.unassigned == 0);
  assert(full.assigned == complete.size());
}

// A role the set binds that no cell selects is a gap in the other direction:
// the palette carries a material this terrain never exposes.
void unusedRoleIsReported() {
  auto scene = plainScene();
  const auto set = fullSet();
  makeWetCell(scene, 4, 4);
  scene.field.heights.set(cellAt(scene, 6, 6), -2.F);
  scene.masks.waterDistance.set(cellAt(scene, 6, 6), 0.F);
  scene.masks.substrate.set(cellAt(scene, 6, 6), std::size_t{3});
  scene.masks.slope.set(cellAt(scene, 1, 1), 45.F);
  const auto decisions =
      decideTerrainMaterials(set, scene.field, scene.masks, scene.settings);
  const auto coverage = terrainMaterialCoverage(set, scene.field, decisions);
  // Nothing on a flat, low, still field is a cliff, and nothing eroded it.
  assert(listed(coverage.unused, "cliff"));
  assert(listed(coverage.unused, "sediment"));
  assert(listed(coverage.unused, "snow"));
  // Roles that were selected are not gaps in the other direction.
  assert(!listed(coverage.unused, "rock"));
  assert(!listed(coverage.unused, "ground"));
  assert(coverage.unbound.empty());
  assert(coverage.unassigned == 0);
  assert(coverage.assigned == decisions.size());
  // The lists are sorted by role name, so two runs report the same gaps in the
  // same order for a diff or an editor list.
  assert(std::is_sorted(coverage.unused.begin(), coverage.unused.end()));
}

// Deposition and flow are what make a flat riverbed sediment rather than soil,
// so the mask that says "material was laid down here" reaches the decision.
void depositionAndFlowAreSediment() {
  auto scene = plainScene();
  const auto set = fullSet();
  const auto deposited = cellAt(scene, 2, 2);
  assert(roleAt(scene, set, deposited) == "ground");
  // A tenth of the field's relief: with no relief the scale falls back to the
  // smallest usable one, so any positive balance is a full deposition.
  scene.masks.sediment.set(deposited, 1.F);
  assert(roleAt(scene, set, deposited) == "sediment");
  scene.masks.sediment.set(deposited, 0.F);
  scene.masks.flow.set(deposited, 1.F);
  assert(roleAt(scene, set, deposited) == "sediment");
  // Deposition on a face is not deposition, it is the face.
  scene.masks.slope.set(deposited, 45.F);
  assert(roleAt(scene, set, deposited) == "rock");
}

// The derived substrate family is a mask like any other, and vegetation does
// not grow on the families that are not soil.
void substrateGatesVegetation() {
  auto scene = plainScene();
  const auto set = fullSet();
  const auto cell = cellAt(scene, 4, 4);
  scene.masks.moisture.set(cell, 0.55F);
  scene.masks.slope.set(cell, 20.F);
  assert(roleAt(scene, set, cell) == "ground");
  scene.masks.moisture.set(cell, 0.95F);
  assert(roleAt(scene, set, cell) == "grass");
  scene.masks.substrate.set(cell, std::size_t{2}); // Sand
  assert(roleAt(scene, set, cell) == "ground");
  scene.masks.substrate.set(cell, std::size_t{3}); // Wet
  assert(roleAt(scene, set, cell) == "ground");
  // A substrate value outside the vocabulary is ignored rather than read as
  // some arbitrary family.
  scene.masks.substrate.set(cell, std::size_t{99});
  assert(roleAt(scene, set, cell) == "grass");
}

// The two scales are the author's handle on the whole decision: one bends every
// slope threshold at once, the other dries or wets every normalised reading.
void settingsScaleTheDecision() {
  auto scene = plainScene();
  const auto set = fullSet();
  const auto shallow = cellAt(scene, 2, 2);
  scene.masks.slope.set(shallow, 20.F);
  assert(roleAt(scene, set, shallow) == "ground");
  auto steep = scene.settings;
  steep.slopeScale = 3.F;
  assert(roleAt(scene, set, shallow, &steep) == "rock");
  auto flat = scene.settings;
  flat.slopeScale = 0.F;
  assert(roleAt(scene, set, shallow, &flat) == "ground");

  auto wet = plainScene();
  makeWetCell(wet, 6, 6);
  const auto shore = cellAt(wet, 6, 6);
  assert(roleAt(wet, set, shore) == "wet_ground");
  auto dry = wet.settings;
  dry.wetnessScale = 0.F;
  // A dry climate keeps the same shoreline but reads it as a dry beach.
  assert(roleAt(wet, set, shore, &dry) == "sand");
  auto soaked = wet.settings;
  soaked.wetnessScale = 2.F;
  assert(roleAt(wet, set, shore, &soaked) == "wet_ground");
}

// The water level is resolved once, by the caller, from the same two sources
// the rest of the terrain pipeline uses. A stage that derived its own would be
// the second answer to "what counts as water".
void waterLevelIsResolvedNotInvented() {
  TerrainRecipe recipe;
  recipe.landforms.at("default").baseHeight = 20;
  recipe.landforms.at("default").heightVariation = 4;
  TerrainWaterLevel unauthored;
  const auto derived = TerrainMaterialLayerSettings::withWaterLevel({}, recipe,
                                                                   unauthored);
  assert(derived.waterLevel == 6.F);

  TerrainWaterLevel authored;
  authored.seaLevel = -2.F;
  authored.authored = true;
  const auto resolved = TerrainMaterialLayerSettings::withWaterLevel({}, recipe,
                                                                    authored);
  assert(resolved.waterLevel == -2.F);

  auto scene = plainScene();
  scene.field.heights.assign(sampleCount(), -4.F);
  const auto submerged = TerrainMaterialLayerSettings::withWaterLevel(
      scene.settings, recipe, authored);
  const auto set = fullSet();
  const auto decisions =
      decideTerrainMaterials(set, scene.field, scene.masks, submerged);
  for (const auto &decision : decisions)
    assert(decision.roleName == "underwater");
}

// Masks on the wrong grid are refused rather than resampled, and so is a set
// that binds nothing: both would otherwise produce a field with no materials
// and no explanation.
void unusableInputsAreRefused() {
  const auto scene = plainScene();
  const auto set = fullSet();

  TerrainMasks wrongGrid;
  wrongGrid.slope.assign(16, 0.F);
  wrongGrid.moisture.assign(sampleCount(), 0.2F);
  bool threw = false;
  try {
    (void)decideTerrainMaterials(set, scene.field, wrongGrid, scene.settings);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
  threw = false;
  try {
    (void)decideTerrainMaterial(set, scene.field, wrongGrid, 0, scene.settings);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  // A partly populated mask set is the same mistake on one map: reading the
  // samples that happen to exist would fabricate the rest.
  auto partial = scene.masks;
  partial.moisture.assign(16, 0.2F);
  threw = false;
  try {
    (void)decideTerrainMaterials(set, scene.field, partial, scene.settings);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  // A field with fewer samples than its masks.
  HeightField broken = scene.field;
  broken.heights.assign(4, 10.F);
  threw = false;
  try {
    (void)decideTerrainMaterials(set, broken, scene.masks, scene.settings);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  demi::assets::TerrainMaterialSet empty;
  empty.id = "asset://materials/terrain/empty";
  threw = false;
  try {
    (void)decideTerrainMaterials(empty, scene.field, scene.masks,
                                 scene.settings);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
  threw = false;
  try {
    (void)terrainMaterialCoverage(empty, scene.field, {});
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
}

// A cell the field cannot produce is a caller error, not an unassigned cell: it
// has no masks, so nothing could be measured there.
void outOfRangeCellIsRefused() {
  const auto scene = plainScene();
  const auto set = fullSet();
  bool threw = false;
  try {
    (void)decideTerrainMaterial(set, scene.field, scene.masks, sampleCount(),
                                scene.settings);
  } catch (const std::out_of_range &) {
    threw = true;
  }
  assert(threw);
}

// Coverage is a statement about the whole field, so a partial resolve is
// refused rather than reported as full coverage.
void coverageNeedsTheWholeField() {
  const auto scene = plainScene();
  const auto set = fullSet();
  const auto decisions =
      decideTerrainMaterials(set, scene.field, scene.masks, scene.settings);
  std::vector<TerrainMaterialDecision> partial(decisions.begin(),
                                              decisions.begin() + 10);
  bool threw = false;
  try {
    (void)terrainMaterialCoverage(set, scene.field, partial);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
}

// The candidate order is the contract behind `role`: the set's own roles first,
// in its key order, then the vocabulary roles it does not bind. An index that
// only meant "the set's roles" would make an unbound role unrepresentable.
void candidateRolesCoverTheVocabulary() {
  const auto set = setWithout("snow");
  const auto candidates = terrainMaterialCandidateRoles(set);
  assert(candidates.size() == 9);
  // The set's roles come first, in the key order the role map iterates, and the
  // one role it does not bind is appended. A bound role's index therefore never
  // moves when the palette loses a role, which is what lets a consumer hold an
  // index across a reload.
  const TerrainMaterialRole expected[] = {
      TerrainMaterialRole::Cliff,   TerrainMaterialRole::Grass,
      TerrainMaterialRole::Ground,  TerrainMaterialRole::Rock,
      TerrainMaterialRole::Sand,    TerrainMaterialRole::Sediment,
      TerrainMaterialRole::Underwater, TerrainMaterialRole::WetGround,
      TerrainMaterialRole::Snow};
  for (std::size_t index = 0; index < 8; ++index)
    assert(candidates[index] == expected[index]);
  assert(candidates[8] == TerrainMaterialRole::Snow);
  // Every role in the vocabulary appears exactly once, whether the set binds it
  // or not, so no role can be scored twice or go missing.
  std::vector<std::string> names;
  for (const TerrainMaterialRole role : candidates)
    names.push_back(std::string(demi::assets::terrainMaterialRoleName(role)));
  std::sort(names.begin(), names.end());
  assert(std::unique(names.begin(), names.end()) == names.end());
  for (const char *role :
       {"rock", "cliff", "sediment", "ground", "grass", "sand", "snow",
        "wet_ground", "underwater"})
    assert(listed(names, role));

  const auto scene = plainScene();
  const auto decisions = decideTerrainMaterials(
      setWithout("ground"), scene.field, scene.masks, scene.settings);
  for (const auto &decision : decisions) {
    assert(decision.roleName == "ground");
    assert(decision.materialAssetId.empty());
  }
  const auto coverage =
      terrainMaterialCoverage(setWithout("ground"), scene.field, decisions);
  assert(listed(coverage.unbound, "ground"));
  assert(coverage.unassigned == decisions.size());
  assert(coverage.assigned == 0);
}

// The winner comes from the scores and never from the order the set stores its
// roles in. Ties fall to the earlier role in the fixed precedence table, so two
// implementations that agreed on the scores still agree on the answer.
void decisionDoesNotDependOnSetOrder() {
  const auto forward = fullSet();
  // The same bindings stored back to front. A key-sorted map iterates the same
  // way either way, which is the property this documents; if the role map ever
  // became an insertion-ordered container, this is the assertion that fails.
  demi::assets::TerrainMaterialSet reversed;
  reversed.id = forward.id;
  for (auto entry = forward.roles.rbegin(); entry != forward.roles.rend();
       ++entry)
    reversed.roles.emplace_hint(reversed.roles.end(), entry->first,
                                entry->second);
  assert(reversed.roles == forward.roles);

  // A damp cell inside the wet band, where the sand and wet-ground bands both
  // score: the winner there is decided by the score, not by which of the two
  // roles happens to come first in the map.
  auto scene = plainScene();
  makeWetCell(scene, 4, 4);
  const auto cell = cellAt(scene, 4, 4);
  const auto one =
      decideTerrainMaterials(forward, scene.field, scene.masks, scene.settings);
  const auto two = decideTerrainMaterials(reversed, scene.field, scene.masks,
                                          scene.settings);
  assert(one[cell].roleName == "wet_ground");
  for (std::size_t index = 0; index < one.size(); ++index) {
    assert(one[index].role == two[index].role);
    assert(one[index].roleName == two[index].roleName);
    assert(one[index].score == two[index].score);
  }
}

// The role a cell wears and the material behind it are separate facts: an
// editor reads the first to explain a surface and the second to bind a texture.
void decisionsNameBothTheRoleAndTheMaterial() {
  auto scene = plainScene();
  const auto set = fullSet();
  scene.masks.slope.set(cellAt(scene, 1, 1), 45.F);
  const auto decisions =
      decideTerrainMaterials(set, scene.field, scene.masks, scene.settings);
  const auto rock = decisions[cellAt(scene, 1, 1)];
  assert(rock.roleName == "rock");
  assert(rock.materialAssetId == "asset://materials/terrain/rock");
  assert(demi::assets::terrainMaterialRoleName(
             terrainMaterialCandidateRoles(set)[rock.role]) == "rock");
  // Nothing here consults the field's biome names: a cell is assigned from the
  // masks alone, so renaming a biome cannot repaint the ground.
  assert(scene.field.biomeIds == std::vector<std::string>{"default"});
}

} // namespace

int main() {
  dryOpenGroundIsGround();
  steepGroundIsRock();
  flatWetCellIsWetGround();
  farFromWaterIsNotWet();
  submergedDiffersFromDamp();
  dryBeachIsSand();
  highFlatGroundIsSnow();
  highSteepGroundIsNotSnow();
  allowSnowLineCanBeDisabled();
  snowLineIsDerivedOrAuthored();
  decisionsDoNotDependOnWorldScale();
  decisionsAreReproducible();
  queryOrderDoesNotChangeDecisions();
  unboundRoleIsReported();
  unusedRoleIsReported();
  depositionAndFlowAreSediment();
  substrateGatesVegetation();
  settingsScaleTheDecision();
  waterLevelIsResolvedNotInvented();
  unusableInputsAreRefused();
  outOfRangeCellIsRefused();
  coverageNeedsTheWholeField();
  candidateRolesCoverTheVocabulary();
  decisionDoesNotDependOnSetOrder();
  decisionsNameBothTheRoleAndTheMaterial();
  std::cout << "Terrain material layers checks passed\n";
}
