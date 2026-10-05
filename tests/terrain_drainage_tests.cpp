#include "demi/runtime/terrain/TerrainDrainage.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using namespace demi::runtime;

namespace {

// Fields are built to a named shape rather than generated from noise, so each
// test asserts on the surface it asked for instead of on whatever the generator
// happened to produce for a seed.
template <class Shape> HeightField makeField(int cellsX, int cellsZ, Shape shape) {
  HeightField field;
  field.size = {float(cellsX), float(cellsZ)};
  field.cellsX = cellsX;
  field.cellsZ = cellsZ;
  const auto count = std::size_t(cellsX + 1) * std::size_t(cellsZ + 1);
  field.heights.resize(count);
  field.baseHeights.resize(count);
  for (int z = 0; z <= cellsZ; ++z)
    for (int x = 0; x <= cellsX; ++x) {
      const auto cell = std::size_t(z) * (cellsX + 1) + std::size_t(x);
      field.heights.set(cell, shape(x, z));
      field.baseHeights.set(cell, shape(x, z));
    }
  return field;
}

TerrainRecipe recipeFor(const HeightField &field) {
  TerrainRecipe recipe;
  recipe.size = field.size;
  recipe.cellsX = field.cellsX;
  recipe.cellsZ = field.cellsZ;
  return recipe;
}

TerrainDrainage drain(const HeightField &field, float seaLevel = 0.F) {
  TerrainDrainageSettings settings;
  settings.seaLevel = seaLevel;
  const auto result =
      computeTerrainDrainage(field, recipeFor(field), settings);
  assert(result);
  return *result;
}

bool border(const HeightField &field, int x, int z) {
  return x == 0 || z == 0 || x == field.cellsX || z == field.cellsZ;
}

// Strictly downhill towards +x and +z: no flat steps and nothing the flood has
// to fill, so every sample's water leaves the field.
float tilted(int x, int z) { return float(x + z); }

// A slope draining outward, with one rimmed pit dug into it: the ground climbs
// from the pit floor to a rim at d = 3 and then falls away to the border, so the
// pit ponds and nothing else does.
//
// A plain bowl is not enough to test a basin here. The border is the outlet, so
// a bowl whose floor is the lowest point in the field just drains off the
// nearest edge and has no closed depression to find.
float slopeWithPit(int x, int z) {
  const auto d = std::hypot(float(x) - 8.F, float(z) - 8.F);
  const float rim = 3.F;
  const auto g = d <= rim ? 10.F * ((d / rim) * (d / rim) - 1.F)
                          : -2.F * (d - rim);
  return 0.35F * (float(x) + float(z)) + g;
}

// Two pits, each draining outward past its own side of the border, with a ridge
// between them standing above both spill levels. min() of two pit profiles would
// not do: the saddle between them would fall below the fill level and weld the
// two into one basin, which would make the labelling untestable.
float twinPits(int x, int z) {
  const auto dx = float(x);
  const auto dz = float(z) - 8.F;
  const auto u = std::fabs(dx - 8.5F) / 3.5F;
  auto height = u <= 1.F ? 12.F * (1.F - u) : -0.4F * (u - 1.F);
  for (const float centre : {5.F, 12.F}) {
    const auto d = std::hypot(dx - centre, dz);
    if (d < 2.5F)
      height += -8.F * (1.F - (d / 2.5F) * (d / 2.5F));
  }
  return height;
}

// A lake in one corner, rising away from it. Water is at or below sea level.
float coastalLake(int x, int z) {
  if (x < 5 && z < 5)
    return -2.F;
  return float(x + z) * 0.5F;
}

float flat(int, int) { return 5.F; }

// Accumulation is a property of the surface: the border contributes nothing to
// report, and every inland sample drains somewhere.
void flowIsZeroOnTheBorderAndPositiveInland() {
  const auto field = makeField(16, 16, tilted);
  const auto drainage = drain(field);
  assert(drainage.flow.size() == field.heights.size());
  std::size_t inland = 0;
  for (int z = 0; z <= field.cellsZ; ++z)
    for (int x = 0; x <= field.cellsX; ++x) {
      const auto cell = field.index(x, z);
      const auto flow = drainage.flow[cell];
      // A border sample is the field's own outlet, with no upstream area inside
      // the field behind it.
      if (border(field, x, z)) {
        assert(flow == 0.F);
      } else {
        assert(flow > 0.F);
        ++inland;
      }
    }
  assert(inland > 0);
}

// Normalisation is against the field's own maximum, so the mask is comparable
// between fields and never exceeds its own stated range.
void flowIsNormalisedToTheFieldMaximum() {
  const auto field = makeField(16, 16, slopeWithPit);
  const auto drainage = drain(field);
  float highest = 0.F;
  std::size_t peak = 0;
  for (std::size_t i = 0; i < drainage.flow.size(); ++i) {
    const auto flow = drainage.flow[i];
    assert(std::isfinite(flow));
    assert(flow >= 0.F && flow <= 1.F);
    if (flow > highest) {
      highest = flow;
      peak = i;
    }
  }
  assert(highest == 1.F);
  // The maximum is reached inside the field: the edge reports zero, so it can
  // never be what defines the normalisation.
  assert(!border(field, int(peak % 17), int(peak / 17)));
}

// One pit means one basin, covering the depression and nothing else.
void aSingleBowlHasExactlyOneBasin() {
  const auto field = makeField(16, 16, slopeWithPit);
  const auto drainage = drain(field);
  assert(drainage.basinCount == 1);
  // Basin ids run 1..basinCount, with the field's own outlet held at index 0.
  assert(drainage.basinOutlets.size() == 2);
  std::size_t pooled = 0, drained = 0;
  for (int z = 0; z <= field.cellsZ; ++z)
    for (int x = 0; x <= field.cellsX; ++x) {
      const auto cell = field.index(x, z);
      const auto d = std::hypot(float(x) - 8.F, float(z) - 8.F);
      // Well inside the rim the ground is standing water, and well outside it
      // the ground drains off the edge. The samples straddling the rim are left
      // out of the comparison so the test states the property rather than an
      // exact fill boundary.
      if (d <= 2.F) {
        assert(drainage.basin[cell] == 1);
        ++pooled;
      } else if (d >= 4.F) {
        assert(drainage.basin[cell] == 0);
        ++drained;
      }
      assert(drainage.basin[cell] <= drainage.basinCount);
    }
  assert(pooled > 0 && drained > 0);
  // The outlet is the pit floor: the lowest sample of the depression, and so the
  // one that identifies where its water collects.
  const auto &outlet = drainage.basinOutlets[1];
  assert(outlet.x == field.position(8, 8).x);
  assert(outlet.z == field.position(8, 8).y);
  assert(outlet.y == field.heights[field.index(8, 8)]);
  // The field itself leaves by its lowest border sample, not by the pit.
  assert(drainage.basin[field.index(0, 0)] == 0);
  assert(drainage.basinOutlets[0].y == field.heights[field.index(0, 0)]);
}

// A perfectly flat field is where a naive flood spins: there is no lower
// neighbour to drain into anywhere. It must still return, and it must return a
// usable surface rather than a field full of directions that go nowhere.
void aFlatFieldTerminates() {
  const auto field = makeField(16, 16, flat);
  const auto drainage = drain(field);
  float highest = 0.F;
  for (std::size_t i = 0; i < drainage.flow.size(); ++i) {
    assert(std::isfinite(drainage.flow[i]));
    highest = std::max(highest, drainage.flow[i]);
  }
  // Water still has to go somewhere, so the accumulation is real.
  assert(highest == 1.F);
  // A flat field drains off its edge rather than ponding, so it has no closed
  // depression to report.
  assert(drainage.basinCount == 0);
  // Every direction is a real index or the explicit outlet sentinel, and no
  // sample drains into itself.
  for (std::size_t i = 0; i < drainage.downstream.size(); ++i) {
    const auto next = drainage.downstream[i];
    assert(next == terrainNoDownstream || next < drainage.downstream.size());
    assert(next != i);
  }
}

// Identical input must produce identical ids. Basin numbering is assigned by
// scanning, so it cannot depend on the order the flood happened to pop cells in,
// which a hash-ordered frontier would.
void basinLabelsAreIdenticalAcrossRuns() {
  const auto field = makeField(16, 16, twinPits);
  const auto first = drain(field);
  const auto second = drain(field);
  // Two pits means the labelling actually has something to get wrong.
  assert(first.basinCount == 2);
  assert(first.basin == second.basin);
  assert(first.flow == second.flow);
  assert(first.downstream == second.downstream);
  assert(first.basinCount == second.basinCount);
  assert(first.basinOutlets.size() == second.basinOutlets.size());
  for (std::size_t i = 0; i < first.basinOutlets.size(); ++i) {
    const auto &a = first.basinOutlets[i];
    const auto &b = second.basinOutlets[i];
    assert(a.x == b.x && a.y == b.y && a.z == b.z);
  }
  // Id 0 is the field's own outlet; the two pits are basins 1 and 2, numbered in
  // row-major order of first encounter.
  assert(first.basin[field.index(5, 8)] == 1);
  assert(first.basin[field.index(12, 8)] == 2);
  assert(first.basin[field.index(0, 0)] == 0);
  // The ridge between the pits belongs to neither, so the basins are genuinely
  // separate rather than one pool split by a labelling accident.
  assert(first.basin[field.index(8, 8)] == 0);
}

// The drainage tree is a forest: following downstream always reaches an outlet,
// so no chain can loop.
void downstreamChainsAlwaysTerminate() {
  const auto field = makeField(16, 16, twinPits);
  const auto drainage = drain(field);
  for (int z = 0; z <= field.cellsZ; ++z)
    for (int x = 0; x <= field.cellsX; ++x) {
      auto cell = field.index(x, z);
      // Bounded by the sample count, so a cycle would trip this rather than
      // hang the test.
      for (auto step = 0; step <= field.heights.size(); ++step) {
        const auto next = drainage.downstream[cell];
        if (next == terrainNoDownstream)
          break;
        cell = next;
        assert(step < field.heights.size());
      }
    }
}

// Water distance is a real distance field: zero in the water, growing inland,
// and spanning more than a single cell.
void waterDistanceIsZeroInWaterAndGrowsInland() {
  const auto field = makeField(16, 16, coastalLake);
  const auto drainage = drain(field, 0.F);
  std::size_t wet = 0;
  float widest = 0.F;
  for (std::size_t i = 0; i < drainage.waterDistance.size(); ++i) {
    const auto distance = drainage.waterDistance[i];
    assert(std::isfinite(distance));
    if (field.heights[i] <= 0.F) {
      assert(distance == 0.F);
      ++wet;
    } else {
      assert(distance > 0.F);
    }
    widest = std::max(widest, distance);
  }
  assert(wet > 0);
  // The transform has to actually spread, not just mark the shoreline.
  assert(widest > field.size.x / float(field.cellsX));
}

// With no water anywhere the whole field is maximally distant from it. Zero
// would make a shoreline rule match every dry sample.
void dryFieldIsMaximallyDistantFromWater() {
  const auto field = makeField(16, 16, flat);
  const auto drainage = drain(field, 0.F);
  const auto unreachable = std::numeric_limits<float>::max() / 4.F;
  for (std::size_t i = 0; i < drainage.waterDistance.size(); ++i)
    assert(drainage.waterDistance[i] == unreachable);

  // A dry field that is not flat behaves the same way.
  const auto tiltedField = makeField(16, 16, tilted);
  const auto dryTilted = drain(tiltedField, -1000.F);
  for (std::size_t i = 0; i < dryTilted.waterDistance.size(); ++i)
    assert(dryTilted.waterDistance[i] == unreachable);
}

// Drainage is computed over a grid, so a field whose samples do not fill its own
// grid has to be refused rather than silently half-processed.
void gridMismatchIsRejected() {
  const auto field = makeField(16, 16, slopeWithPit);
  HeightField broken = field;
  broken.heights.resize(field.heights.size() / 2);
  bool failed = false;
  try {
    (void)computeTerrainDrainage(broken, recipeFor(broken),
                                 TerrainDrainageSettings{});
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);

  // A recipe that does not describe this field is a different mistake and is
  // equally refused rather than resolved against whichever grid was meant.
  auto wrongRecipe = recipeFor(field);
  wrongRecipe.cellsX = 8;
  bool recipeFailed = false;
  try {
    (void)computeTerrainDrainage(field, wrongRecipe, TerrainDrainageSettings{});
  } catch (const std::invalid_argument &) {
    recipeFailed = true;
  }
  assert(recipeFailed);
}
} // namespace

int main() {
  flowIsZeroOnTheBorderAndPositiveInland();
  flowIsNormalisedToTheFieldMaximum();
  aSingleBowlHasExactlyOneBasin();
  aFlatFieldTerminates();
  basinLabelsAreIdenticalAcrossRuns();
  downstreamChainsAlwaysTerminate();
  waterDistanceIsZeroInWaterAndGrowsInland();
  dryFieldIsMaximallyDistantFromWater();
  gridMismatchIsRejected();
  std::cout << "Terrain drainage checks passed\n";
}
