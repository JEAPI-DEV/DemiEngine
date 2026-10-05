#include "demi/runtime/terrain/TerrainScatterConstraints.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace demi::runtime;

namespace {

// A 64x64 world on an 8-cell grid: nine samples per axis at 0, 8, ... 64. Small
// enough that every assertion names a cell by its world position and reads as a
// claim about the terrain rather than about index arithmetic.
constexpr float WorldSize = 64.F;
constexpr int Cells = 8;

struct Scene {
  HeightField field;
  TerrainRecipe recipe;
  TerrainMasks masks;
  TerrainWaterLevel water;
  TerrainScatterConstraintOptions options;
};

std::size_t samples() {
  return std::size_t(Cells + 1) * std::size_t(Cells + 1);
}

// The cell whose sample sits at this world position. Positions are cell
// multiples of 8, so a test never has to talk about a half cell.
std::size_t cellAt(float x, float z) {
  const auto column = int(x / (WorldSize / float(Cells)));
  const auto row = int(z / (WorldSize / float(Cells)));
  return std::size_t(row) * std::size_t(Cells + 1) + std::size_t(column);
}

// Dry, flat, unexcluded ground with an authored water level of zero, so every
// constraint under test has to be the one that actually rejects a cell.
Scene dryScene(float height = 10.F) {
  Scene scene;
  scene.field.size = {WorldSize, WorldSize};
  scene.field.cellsX = scene.field.cellsZ = Cells;
  scene.field.baseHeights.assign(samples(), height);
  scene.field.heights = scene.field.baseHeights;
  scene.field.normals.assign(samples(), Vec3{0, 1, 0});
  scene.field.biomeIndices.assign(samples(), std::size_t{0});
  scene.field.exclusions.assign(samples(), 0.F);
  scene.field.biomeIds = {"default"};
  scene.field.biomeColors = {Color{}};
  // Only the unauthored sea-level derivation reads the recipe, and this scene
  // authors its water level, so the landform exists only to be a valid recipe.
  scene.recipe.landforms.at("default").baseHeight = 20;
  scene.recipe.landforms.at("default").heightVariation = 4;
  scene.water.seaLevel = 0;
  scene.water.authored = true;
  scene.masks.slope.assign(samples(), 0.F);
  scene.masks.moisture.assign(samples(), 0.F);
  scene.masks.waterDistance.assign(samples(), 1000.F);
  scene.masks.flow.assign(samples(), 0.F);
  scene.masks.sediment.assign(samples(), 0.F);
  scene.masks.substrate.assign(samples(), std::size_t{0});
  return scene;
}

// An L-shaped road: one segment along z = 24 from x = 0 to 24, then one along
// x = 24 from z = 24 to 64. Width 4, so the tested reach is two world units
// either side of the centreline.
void addTestRoad(TerrainScatterConstraints &constraints, float height) {
  constraints.addRoad({{0, height, 24}, {24, height, 24}, {24, height, 64}}, 4.F);
}

// Open ground is the baseline every other assertion depends on: a constraint
// that reported a reason here would make all of them vacuous.
void openGroundIsAllowed() {
  const auto scene = dryScene();
  const auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  for (std::size_t cell = 0; cell < constraints.cellCount(); ++cell) {
    assert(!constraints.blocked(cell));
    assert(constraints.reason(cell).empty());
  }
  assert(constraints.blockedCount() == 0);
  assert(constraints.shapeCount() == 0);
}

// Below the water level is water, and so is a sample exactly on it: the solver
// treats the shoreline the same way, and a tree planted in the surf is a visible
// bug rather than a defensible reading of "below".
void waterBlocks() {
  auto scene = dryScene();
  scene.field.heights.set(cellAt(16, 16), -2.F);
  scene.water.seaLevel = 0;
  const auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  assert(constraints.reason(cellAt(16, 16)) == "water");
  assert(!constraints.blocked(cellAt(40, 40)));

  auto shoreline = dryScene(0.F);
  const auto flooded = TerrainScatterConstraints::build(
      shoreline.field, shoreline.recipe, &shoreline.masks, shoreline.water,
      shoreline.options);
  assert(flooded.reason(cellAt(0, 0)) == "water");
  // Every sample is at the level, so the whole grid is water. That is the
  // invariant a coverage preview reads, and it must not be a per-cell guess.
  assert(flooded.blockedCount() == flooded.cellCount());
}

// An authored water surface above the sea level gets its own reason, because the
// answer an author needs about a perched lake is not "sea level".
void waterSurfaceBlocks() {
  auto scene = dryScene();
  scene.options.submerged = [](Vec3 position, float) {
    return position.x == 32.F && position.z == 32.F;
  };
  const auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  assert(constraints.reason(cellAt(32, 32)) == "water surface");
  assert(!constraints.blocked(cellAt(32, 40)));
  // The test was consulted once per cell while building, not once per query: a
  // caller whose test is expensive would otherwise pay for it on every
  // placement candidate.
  assert(constraints.reason(cellAt(48, 48)).empty());
}

// Slope is a bound, so the value on the bound is rejected and a degree under it
// is not. Anything else would make the authored maximum a suggestion.
void steepSlopeBlocks() {
  auto scene = dryScene();
  scene.masks.slope.set(cellAt(24, 40), 40.F);
  scene.masks.slope.set(cellAt(40, 24), 39.9F);
  const auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  assert(constraints.reason(cellAt(24, 40)) == "slope");
  assert(!constraints.blocked(cellAt(40, 24)));
  assert(constraints.maximumSlopeDegrees() == 40.F);

  // With no masks stage there is no slope to read, and no invented flat one
  // either: the constraint is absent rather than claiming the ground is level.
  scene.field.heights.set(cellAt(8, 8), -2.F);
  const auto withoutMasks = TerrainScatterConstraints::build(
      scene.field, scene.recipe, nullptr, scene.water, scene.options);
  assert(!withoutMasks.blocked(cellAt(24, 40)));
  // Water and the terrain's own exclusions still apply without masks.
  assert(withoutMasks.reason(cellAt(8, 8)) == "water");
}

// The recipe's exclusion paint is a different author from a brush stroke added
// at runtime, so it gets its own word instead of sharing one with it.
void terrainExclusionBlocks() {
  auto scene = dryScene();
  scene.field.exclusions.set(cellAt(8, 56), 1.F);
  const auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  assert(constraints.reason(cellAt(8, 56)) == "terrain exclusion");
  assert(!constraints.blocked(cellAt(16, 56)));
}

// A brush stroke is not a road and not a footprint, and the difference has to
// survive into the reason: an editor that says "road" for a painted blob sends
// the author looking for a road document that does not exist.
void paintedExclusionBlocks() {
  auto scene = dryScene();
  auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  const auto painted = cellAt(48, 8);
  constraints.addExclusionWeight(painted, 1.F);
  assert(constraints.reason(painted) == "painted exclusion");

  // A weight under the threshold is not an exclusion at all. Recording it as a
  // pending one would leave a cell that reads as excluded but names no reason.
  const auto faint = cellAt(48, 16);
  constraints.addExclusionWeight(faint, 0.0001F);
  assert(!constraints.blocked(faint));
  assert(constraints.reason(faint).empty());

  // Repainting the same cell to a real weight takes effect immediately.
  constraints.addExclusionWeight(faint, 0.5F);
  assert(constraints.blocked(faint));
}

void protectedAreaBlocks() {
  auto scene = dryScene();
  auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  constraints.addProtectedArea({56, 56}, 8.F);
  assert(constraints.reason(cellAt(56, 56)) == "protected area");
  // On the boundary counts as protected: a tree half in a reserved site is in
  // the site. Both the axial and the diagonal cell sit exactly eight out.
  assert(constraints.blocked(cellAt(56, 48)));
  assert(constraints.blocked(cellAt(48, 56)));
  // A cell further out is not.
  assert(!constraints.blocked(cellAt(56, 32)));
  assert(!constraints.blocked(cellAt(40, 56)));
}

// The test that decides whether roads are modelled at all. A point inside the
// road's bounding box but far from its centreline is open ground, and a
// bounding-box test would reject it: that is what turns a road into a wall
// around its own bend.
void roadBlocksByCentreline() {
  auto scene = dryScene();
  auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  addTestRoad(constraints, 10.F);

  // On the centreline, and on the corner where the two segments meet.
  assert(constraints.reason(cellAt(0, 24)) == "road");
  assert(constraints.reason(cellAt(24, 24)) == "road");
  assert(constraints.reason(cellAt(24, 64)) == "road");

  // Inside the bounding box x in [0,24], z in [24,64], but nowhere near either
  // segment. A bounding box rejects all of these.
  assert(!constraints.blocked(cellAt(0, 32)));
  assert(!constraints.blocked(cellAt(8, 32)));
  assert(!constraints.blocked(cellAt(0, 56)));
  assert(!constraints.blocked(cellAt(16, 64)));
  assert(constraints.reason(cellAt(0, 32)).empty());
  // Outside the bounding box entirely.
  assert(!constraints.blocked(cellAt(32, 8)));

  // The width is the authored width, so half of it either side is still road
  // and one cell further is not. This road is 16 wide, so 8 away is its edge.
  constraints.addRoad({{0, 10, 40}, {16, 10, 40}}, 16.F);
  assert(constraints.reason(cellAt(8, 32)) == "road");
  assert(!constraints.blocked(cellAt(8, 16)));
  // A cell this road now covers was allowed a moment ago, which is the whole
  // point of "adding a constraint can only remove placements".
  assert(!constraints.blocked(cellAt(0, 56)));
}

// A footprint is a polygon, not a disc and not its bounding box. The polygon
// below is an L, so its notch is inside the bounding box and outside the
// building.
void footprintBlocks() {
  auto scene = dryScene();
  auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  constraints.addFootprint(
      {{16, 4}, {32, 4}, {32, 12}, {24, 12}, {24, 28}, {16, 28}});

  // Top bar of the L.
  assert(constraints.reason(cellAt(24, 8)) == "footprint");
  // Left column of the L, exactly on its left edge.
  assert(constraints.reason(cellAt(16, 24)) == "footprint");
  // A point on a wall is inside a solid building.
  assert(constraints.blocked(cellAt(32, 8)));
  // The notch: inside the bounding box, inside the convex hull, outside the
  // building. A hull or box test would wrongly reject this cell.
  assert(!constraints.blocked(cellAt(32, 24)));
  assert(constraints.reason(cellAt(32, 24)).empty());
  // Clear of the building.
  assert(!constraints.blocked(cellAt(40, 8)));
}

// Adding a constraint may only remove placements. A cell water already rejected
// must not be released by the road drawn across it, and the count may never go
// down as constraints arrive.
void constraintsNeverRelax() {
  auto scene = dryScene();
  scene.field.heights.set(cellAt(8, 8), -2.F);
  auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  const auto alreadyBlocked = constraints.blockedCount();
  assert(constraints.blocked(cellAt(8, 8)));
  constraints.addProtectedArea({8, 8}, 1.F);
  assert(constraints.blocked(cellAt(8, 8)));
  // The area now outranks the water in the reason, and the cell was blocked
  // before the area existed and stays blocked after it.
  assert(constraints.reason(cellAt(8, 8)) == "protected area");
  assert(constraints.blockedCount() >= alreadyBlocked);
}

// The reported reason is a contract, not an implementation detail: an editor
// rebuilds every frame and must show the same explanation each time. Each step
// leaves enabled only the kinds at or below its own position in the precedence
// list, so the reason has to walk down that list. A new kind has to be inserted
// here in the right place rather than discovered by an editor showing the wrong
// word for a gap.
void reasonPrecedenceIsFixed() {
  const std::string_view expected[] = {
      "footprint",         "road",
      "protected area",    "painted exclusion",
      "terrain exclusion", "water surface",
      "water",             "slope"};
  const auto target = cellAt(8, 8);
  for (std::size_t step = 0; step < std::size(expected); ++step) {
    // Water is a height test, so the ground has to drop for it and come back up
    // for the last step, which must show slope and nothing else.
    auto scene = dryScene(step == 7 ? 10.F : 0.F);
    scene.masks.slope.set(target, 90.F);
    if (step <= 4)
      scene.field.exclusions.set(target, 1.F);
    if (step <= 5)
      scene.options.submerged = [](Vec3 position, float) {
        return position.x == 8.F && position.z == 8.F;
      };
    auto constraints = TerrainScatterConstraints::build(
        scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
    if (step <= 3)
      constraints.addExclusionWeight(target, 1.F);
    if (step <= 2)
      constraints.addProtectedArea({8, 8}, 2.F);
    if (step <= 1)
      constraints.addRoad({{8, 10, 0}, {8, 10, 16}}, 4.F);
    // The footprint is only ever the whole story, so it appears at one step.
    if (step == 0)
      constraints.addFootprint({{0, 0}, {16, 0}, {16, 16}, {0, 16}});
    assert(constraints.blocked(target));
    assert(constraints.reason(target) == expected[step]);
  }
}

// The count is a whole-grid scan, so it has to be invalidated by every mutation
// or an editor would report a stale coverage after every brush stroke.
void blockedCountTracksEdits() {
  auto scene = dryScene();
  auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  assert(constraints.blockedCount() == 0);
  addTestRoad(constraints, 10.F);
  const auto afterRoad = constraints.blockedCount();
  assert(afterRoad > 0);
  std::size_t manual = 0;
  for (std::size_t cell = 0; cell < constraints.cellCount(); ++cell)
    manual += constraints.blocked(cell) ? 1U : 0U;
  assert(manual == afterRoad);
  // The cached count must be recomputed, not reused, after the next edit. A
  // radius of four reaches exactly one of the eight-unit samples.
  constraints.addProtectedArea({56, 56}, 4.F);
  assert(constraints.blockedCount() == afterRoad + 1);
  constraints.addExclusionWeight(cellAt(48, 48), 1.F);
  assert(constraints.blockedCount() == afterRoad + 2);
}

// An in-progress brush is not a region yet. Anything degenerate is ignored
// rather than rejected, because a drag that starts with one point must not throw.
void degenerateShapesAreIgnored() {
  auto scene = dryScene();
  auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  constraints.addRoad({{8, 0, 8}}, 4.F); // one point
  constraints.addRoad({}, 4.F);          // empty
  constraints.addRoad({{8, 0, 8}, {16, 0, 16}}, 0.F); // no width
  constraints.addRoad({{8, 0, 8}, {nan, 0, 8}}, 4.F);
  constraints.addFootprint({{0, 0}, {1, 1}}); // not a polygon
  constraints.addFootprint({});
  constraints.addFootprint({{0, 0}, {1, 0}, {nan, 1}});
  constraints.addProtectedArea({8, 8}, 0.F); // no extent
  constraints.addProtectedArea({8, 8}, -3.F);
  constraints.addProtectedArea({nan, 8}, 4.F);
  constraints.addExclusionWeight(constraints.cellCount(), 1.F); // outside the grid
  constraints.addExclusionWeight(0, std::numeric_limits<float>::infinity());
  assert(constraints.shapeCount() == 0);
  assert(constraints.blockedCount() == 0);
}

// A cell the generator cannot produce has no ground under it, so it is allowed
// and explains nothing. Inventing an answer would report a constraint failure
// against a sample that does not exist.
void outOfRangeCellIsNotBlocked() {
  auto scene = dryScene();
  const auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  assert(!constraints.blocked(constraints.cellCount()));
  assert(constraints.reason(constraints.cellCount() + 1000).empty());
}

// Masks on the wrong resolution must be refused rather than resampled: measuring
// slope on a grid that does not match the heights is how a constraint ends up
// "not working" with no explanation anywhere.
void gridMismatchIsRefused() {
  auto scene = dryScene();
  const auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  assert(constraints.cellCount() == samples());

  TerrainMasks wrong;
  wrong.slope.assign(16, 0.F);
  bool threw = false;
  try {
    (void)TerrainScatterConstraints::build(scene.field, scene.recipe, &wrong,
                                           scene.water, scene.options);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  HeightField broken = scene.field;
  broken.heights.assign(4, 0.F);
  threw = false;
  try {
    (void)TerrainScatterConstraints::build(broken, scene.recipe, &scene.masks,
                                           scene.water, scene.options);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
}

// An unauthored water level falls back to the level derived from the recipe's
// landforms, which is the same number the scatter solver uses. Two derivations
// of "what counts as water" is the disagreement this whole stage exists to
// prevent.
void unauthoredWaterUsesTheRecipe() {
  auto scene = dryScene();
  scene.water.authored = false;
  // The recipe's landform peaks at 24, so the derived level is a quarter of it.
  const auto derived = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  assert(derived.seaLevel() == 6.F);
  assert(!derived.blocked(cellAt(40, 40)));

  scene.field.heights.set(cellAt(40, 40), 4.F);
  const auto flooded = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  assert(flooded.reason(cellAt(40, 40)) == "water");

  // An authored level wins over the derived one, exactly as an authored
  // protection layer does.
  scene.water.authored = true;
  scene.water.seaLevel = 0;
  const auto authored = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  assert(!authored.blocked(cellAt(40, 40)));
}

// Many roads at once, which is the case a per-cell scan of every segment fails
// on. The answers have to stay exact as the index fills up.
void manyRoadsStayCorrect() {
  auto scene = dryScene();
  auto constraints = TerrainScatterConstraints::build(
      scene.field, scene.recipe, &scene.masks, scene.water, scene.options);
  // Short horizontal roads, two units apart in x and eight in z, so they cover
  // a tenth of the field without any of them overlapping.
  for (int i = 0; i < 200; ++i) {
    const auto x = float(i / 8) * 2.F;
    const auto z = float(i % 8) * 8.F;
    constraints.addRoad({{x, 10.F, z}, {x + 1.F, 10.F, z}}, 2.F);
  }
  assert(constraints.shapeCount() == 200);
  // Directly on a road's centreline.
  assert(constraints.reason(cellAt(0, 0)) == "road");
  assert(constraints.reason(cellAt(24, 0)) == "road");
  assert(constraints.reason(cellAt(48, 56)) == "road");
  // Seven units from the nearest of them, which is past the reach of two.
  assert(!constraints.blocked(cellAt(56, 8)));
  assert(!constraints.blocked(cellAt(56, 56)));
  assert(constraints.reason(cellAt(56, 56)).empty());
}
} // namespace

int main() {
  openGroundIsAllowed();
  waterBlocks();
  waterSurfaceBlocks();
  steepSlopeBlocks();
  terrainExclusionBlocks();
  paintedExclusionBlocks();
  protectedAreaBlocks();
  roadBlocksByCentreline();
  footprintBlocks();
  constraintsNeverRelax();
  reasonPrecedenceIsFixed();
  blockedCountTracksEdits();
  degenerateShapesAreIgnored();
  outOfRangeCellIsNotBlocked();
  gridMismatchIsRefused();
  unauthoredWaterUsesTheRecipe();
  manyRoadsStayCorrect();
  std::cout << "Terrain scatter constraints checks passed\n";
}
