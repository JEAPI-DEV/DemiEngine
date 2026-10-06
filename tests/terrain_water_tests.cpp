#include "demi/runtime/terrain/TerrainWater.h"

#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainMasks.h"
#include "demi/runtime/terrain/TerrainSurface.h"
#include "demi/runtime/terrain/TerrainWaterGeometry.h"
#include "demi/runtime/terrain/TerrainWaterQueries.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace demi::runtime;

namespace {

// A field built from a height function, so every test states the ground it
// means to carve into instead of inheriting a generator's noise. Determinism
// here has to be exact: a river's channel and a lake's shoreline are asserted
// against specific numbers.
HeightField fieldWith(Vec2 size, int cellsX, int cellsZ,
                      const std::function<float(double, double)> &height) {
  HeightField field;
  field.size = size;
  field.cellsX = cellsX;
  field.cellsZ = cellsZ;
  const auto count = std::size_t(cellsX + 1) * std::size_t(cellsZ + 1);
  field.heights.resize(count);
  field.normals.resize(count);
  field.biomeIndices.resize(count);
  field.exclusions.resize(count);
  for (int z = 0; z <= cellsZ; ++z) {
    for (int x = 0; x <= cellsX; ++x) {
      const auto sample = field.index(x, z);
      const auto position = field.position(x, z);
      field.heights.set(sample, height(position.x, position.y));
      field.normals.set(sample, Vec3{0, 1, 0});
      field.biomeIndices.set(sample, 0);
      field.exclusions.set(sample, 0.F);
    }
  }
  field.biomeIds = {"ground"};
  field.biomeColors = {Color{0.4F, 0.4F, 0.3F, 1.F}};
  return field;
}

float distanceFrom(double x, double z, double centerX, double centerZ) {
  return float(std::hypot(x - centerX, z - centerZ));
}

// A bowl centred in the field: ground rises with distance, so the level-0
// waterline is a circle of known radius and the fill depth is known at any
// point. Everything about the lake and shoreline tests is derived from this.
float bowlHeight(double x, double z) { return distanceFrom(x, z, 4, 4) - 3.F; }

HeightField bowlField() { return fieldWith({8, 8}, 8, 8, bowlHeight); }

TerrainWaterBodySpec lake(std::string id, float level, float radius = 0) {
  TerrainWaterBodySpec spec;
  spec.id = std::move(id);
  spec.kind = TerrainWaterBody::Lake;
  spec.level = level;
  spec.radius = radius;
  spec.center = {4, 4};
  return spec;
}

TerrainWaterAuthoring authoringWith(TerrainWaterBodySpec body) {
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  authoring.bodies.push_back(std::move(body));
  return authoring;
}

float carvedAt(const TerrainWaterResult &result, const HeightField &field,
               int x, int z) {
  return result.carvedHeights[field.index(x, z)];
}

double surfaceArea(const TerrainWaterSurface &surface) {
  double area = 0;
  std::set<std::array<std::size_t, 3>> triangles;
  std::set<std::pair<float, float>> positions;
  for (const auto &vertex : surface.vertices)
    assert(positions.emplace(vertex.x, vertex.z).second);
  for (std::size_t i = 0; i < surface.indices.size(); i += 3) {
    std::array<std::size_t, 3> indices{
        surface.indices[i], surface.indices[i + 1], surface.indices[i + 2]};
    const auto a = surface.vertices[indices[0]];
    const auto b = surface.vertices[indices[1]];
    const auto c = surface.vertices[indices[2]];
    const double twiceArea = (double(b.z) - a.z) * (double(c.x) - a.x) -
                             (double(b.x) - a.x) * (double(c.z) - a.z);
    assert(twiceArea > 0);
    std::sort(indices.begin(), indices.end());
    assert(triangles.insert(indices).second);
    area += 0.5 * twiceArea;
  }
  return area;
}

// A river cuts a channel along its path, and the ground there ends up below the
// ground that was there before. The field itself must be byte-identical
// afterwards: a carve that mutated its input could not be undone by re-running
// generation, and nothing else in the pipeline would notice.
void riverCarvesChannelAndLeavesFieldUnchanged() {
  const auto field = bowlField();
  const auto before = field.heights;

  TerrainWaterBodySpec river;
  river.id = "brook";
  river.kind = TerrainWaterBody::River;
  river.level = 0;
  river.riverWidth = 3;
  // Bed below the valley floor, so the cut is unambiguous.
  river.riverPath = {Vec3{1.F, -4.F, 4.F}, Vec3{7.F, -4.F, 4.F}};
  const auto result = *carveTerrainWater(field, authoringWith(river), nullptr);

  // Straight down the middle of the path, the channel is cut to the authored
  // bed, and the ground there is lower than it was.
  assert(carvedAt(result, field, 4, 4) <= -4.F);
  assert(carvedAt(result, field, 4, 4) < field.height(4, 4));
  assert(result.dropped == 0);

  // The width tapers to nothing at both ends, so the cut opens and closes along
  // the path rather than starting and stopping with a cut face.
  assert(carvedAt(result, field, 1, 4) == field.height(1, 4));
  assert(carvedAt(result, field, 7, 4) == field.height(7, 4));
  assert(carvedAt(result, field, 3, 4) < field.height(3, 4));
  assert(carvedAt(result, field, 5, 4) < field.height(5, 4));

  // The ground well outside the authored width is untouched: the carve is local
  // to it, and reaching beyond would be the engine guessing.
  assert(carvedAt(result, field, 4, 0) == field.height(4, 0));
  assert(carvedAt(result, field, 4, 7) == field.height(4, 7));

  // Non-destructive, and provably so rather than incidentally: the input
  // samples still read exactly as they did, and the result is a different
  // buffer.
  for (int z = 0; z <= field.cellsZ; ++z)
    for (int x = 0; x <= field.cellsX; ++x)
      assert(field.height(x, z) == before[field.index(x, z)]);
  assert(!(result.carvedHeights == field.heights));

  // Same input, same answer. A carve whose result depended on visit order
  // could not be cached, previewed or compared.
  const auto again = *carveTerrainWater(field, authoringWith(river), nullptr);
  assert(again.carvedHeights == result.carvedHeights);
}

// Drainage is an input to the carve, not a decoration on it: the same river
// digs deeper where the landform already drains there. A channel that ignored
// the flow mask would contradict the mask a biome rule is reading at the very
// same sample.
void riverDepthFollowsDrainage() {
  const auto field = bowlField();
  TerrainWaterBodySpec river;
  river.id = "brook";
  river.kind = TerrainWaterBody::River;
  river.level = 0;
  river.riverWidth = 4;
  river.riverPath = {Vec3{1.F, -3.5F, 4.F}, Vec3{7.F, -3.5F, 4.F}};
  const auto authoring = authoringWith(river);

  TerrainMasks drainage;
  drainage.flow.resize(field.heights.size());
  // The upstream half of the field accumulates, the downstream half does not.
  for (int z = 0; z <= field.cellsZ; ++z)
    for (int x = 0; x <= field.cellsX; ++x)
      drainage.flow.set(field.index(x, z), x <= 4 ? 1.F : 0.F);

  const auto wet = *carveTerrainWater(field, authoring, &drainage);
  const auto dry = *carveTerrainWater(field, authoring, nullptr);

  // Without drainage the channel is still cut, to the full authored bed, so a
  // project with no drainage stage keeps working rivers rather than losing
  // them.
  assert(carvedAt(dry, field, 4, 4) <= -3.5F);
  assert(carvedAt(dry, field, 4, 4) < field.height(4, 4));

  // A draining cell is cut to the bed; a still one stops short of it, so the
  // channel deepens where the landform already drains there instead of running
  // the same depth everywhere.
  assert(carvedAt(wet, field, 3, 4) <= -3.5F);
  assert(carvedAt(wet, field, 5, 4) > -3.5F);
  assert(carvedAt(wet, field, 5, 4) > carvedAt(dry, field, 5, 4));
  // And it is still a channel, not a trench cut in half.
  assert(carvedAt(wet, field, 5, 4) < field.height(5, 4));

  // Masks whose grid is not the field's are a caller error, reported instead of
  // carving from a resolution the field does not have.
  TerrainMasks wrong;
  wrong.flow.resize(8);
  assert(!carveTerrainWater(field, authoring, &wrong));
}

// A lake fills the basin below its level and its surface is level, whatever the
// ground under it does.
void lakeFillsBasinWithLevelSurface() {
  const auto field = bowlField();
  const auto result =
      *carveTerrainWater(field, authoringWith(lake("tarn", 0, 5)), nullptr);
  assert(result.dropped == 0);
  assert(result.surfaces.size() == 1);
  const auto &surface = result.surfaces.front();
  assert(surface.id == "tarn" && surface.kind == TerrainWaterBody::Lake);
  assert(!surface.vertices.empty());
  assert(surface.indices.size() % 3 == 0);
  assert(surfaceArea(surface) > 0);

  // Level is the whole point of a lake: every vertex sits on one plane.
  for (const auto &vertex : surface.vertices)
    assert(std::fabs(vertex.y - surface.level) < 1e-5F);

  // The basin is genuinely full, and it is genuinely carved: the bed went down
  // everywhere the lake reaches, and a quarter of its fill went with it.
  float deepest = 0;
  for (const auto &depth : surface.depth)
    deepest = std::max(deepest, depth);
  assert(deepest > 0.F);
  assert(carvedAt(result, field, 4, 4) < field.height(4, 4));
  const auto fill = float(0 - field.height(4, 4));
  assert(std::fabs(double(carvedAt(result, field, 4, 4)) -
                   double(field.height(4, 4)) + 0.25 * fill) < 1e-4);

  // And the render-ready surface agrees with the carved ground vertex for
  // vertex, which is the property the renderer's absorption term depends on.
  auto carved = field;
  carved.heights = result.carvedHeights;
  for (std::size_t vertex = 0; vertex < surface.vertices.size(); ++vertex) {
    const auto point = surface.vertices[vertex];
    const auto ground = *sampleTerrainHeight(carved, {point.x, point.z});
    assert(ground <= surface.level + 1e-4F);
    const auto expected = std::max(0.F, surface.level - ground);
    assert(std::fabs(surface.depth[vertex] - expected) < 1e-4F);
  }
  // A normal per vertex: a surface the renderer cannot light is not
  // render-ready.
  assert(surface.normals.size() == surface.vertices.size());
  for (const auto &normal : surface.normals)
    assert(std::fabs(normal.y - 1.F) < 1e-5F);
  // Depth per vertex too, and every index in range.
  assert(surface.depth.size() == surface.vertices.size());
  for (const auto index : surface.indices)
    assert(index < surface.vertices.size());
}

// Depth is positive inside the body and zero at its edge, so absorption ramps
// in and foam has somewhere to start.
void surfaceDepthRampsToZeroAtTheEdge() {
  const auto field = bowlField();
  const auto result =
      *carveTerrainWater(field, authoringWith(lake("tarn", 0, 5)), nullptr);
  const auto &surface = result.surfaces.front();
  bool positive = false;
  bool zero = false;
  for (const auto &depth : surface.depth) {
    assert(depth >= 0.F);
    positive = positive || depth > 0.F;
    zero = zero || depth == 0.F;
  }
  assert(positive && zero);

  // Zero-depth vertices must be actual intersections with the ground plane,
  // including fractional crossings rather than clamped dry grid corners.
  bool onWaterline = false;
  auto carved = field;
  carved.heights = result.carvedHeights;
  for (std::size_t vertex = 0; vertex < surface.vertices.size(); ++vertex) {
    const auto point = surface.vertices[vertex];
    const auto ground = *sampleTerrainHeight(carved, {point.x, point.z});
    assert(ground <= surface.level + 1e-4F);
    if (surface.depth[vertex] == 0.F) {
      assert(std::fabs(ground - surface.level) < 1e-4F);
      onWaterline = onWaterline || std::fabs(ground - surface.level) < 1e-4F;
    }
    // A vertex with real water over it is deeper than the surface claims by
    // more than the sampling error, which is what the renderer's absorption
    // term reads.
    if (surface.depth[vertex] > 1.F)
      assert(ground < surface.level - 1.F);
  }
  assert(onWaterline);
}

// Softening turns a waterline cliff into a slope. It lowers the ground in a
// band near the shore and stops there: it must not creep inland, and it must
// not change which samples are water.
void shorelineSofteningLowersGroundNearTheWaterline() {
  const auto field = bowlField();
  auto soft = lake("tarn", 0, 5);
  soft.shorelineSoftening = true;
  auto hard = lake("tarn", 0, 5);
  hard.shorelineSoftening = false;

  const auto softened = *carveTerrainWater(field, authoringWith(soft), nullptr);
  const auto untouched =
      *carveTerrainWater(field, authoringWith(hard), nullptr);

  // A dry sample just above the waterline: the band reaches it and lowers it.
  // The band has a fixed reach in cells, and the cut is bounded by both the
  // remaining relief and half the band width, so a shore never becomes a
  // trench.
  int softenedSamples = 0;
  for (int z = 0; z <= field.cellsZ; ++z)
    for (int x = 0; x <= field.cellsX; ++x) {
      const auto ground = field.height(x, z);
      if (ground <= 0 || ground > 1.F)
        continue;
      assert(carvedAt(softened, field, x, z) <
             carvedAt(untouched, field, x, z));
      assert(carvedAt(softened, field, x, z) >= ground - 1.F);
      ++softenedSamples;
    }
  assert(softenedSamples > 0);

  // Beyond the band's reach the two carves are identical, and equal to the
  // original ground: a softening pass that kept going would be rewriting the
  // land, not the shore. The water reaches three cells out, and the band is
  // two.
  int beyondBand = 0;
  for (int z = 0; z <= field.cellsZ; ++z)
    for (int x = 0; x <= field.cellsX; ++x) {
      if (distanceFrom(field.position(x, z).x, field.position(x, z).y, 4, 4) <=
          5.2F)
        continue;
      assert(carvedAt(softened, field, x, z) ==
             carvedAt(untouched, field, x, z));
      assert(carvedAt(softened, field, x, z) == field.height(x, z));
      ++beyondBand;
    }
  assert(beyondBand > 0);

  // Which samples are water is decided by the ground and the level, so the band
  // does not move the shoreline: the two bodies cover the same ground.
  const auto softField = terrain_water_detail::buildWaterLevelField(
      field.size, field.cellsX, field.cellsZ, softened.carvedHeights,
      authoringWith(soft));
  const auto hardField = terrain_water_detail::buildWaterLevelField(
      field.size, field.cellsX, field.cellsZ, untouched.carvedHeights,
      authoringWith(hard));
  assert(softField.wet == hardField.wet);
}

// Two bodies over the same ground, and the order the author wrote them in is
// what resolves it. A later body wins the overlap outright rather than
// blending, so the answer is decidable without asking which body "really" owns
// a shared cell.
void bodyOrderDecidesOverlap() {
  const auto field = bowlField();
  TerrainWaterAuthoring lowThenHigh;
  lowThenHigh.authored = true;
  lowThenHigh.bodies.push_back(lake("shallow", -1, 5));
  lowThenHigh.bodies.push_back(lake("deep", 0.5F, 5));
  TerrainWaterAuthoring highThenLow = lowThenHigh;
  std::swap(highThenLow.bodies[0], highThenLow.bodies[1]);

  const auto lowHigh = terrain_water_detail::buildWaterLevelField(
      field.size, field.cellsX, field.cellsZ, field.heights, lowThenHigh);
  const auto highLow = terrain_water_detail::buildWaterLevelField(
      field.size, field.cellsX, field.cellsZ, field.heights, highThenLow);

  // Ground under the shallow level is reached by both bodies, so the winner
  // there is decided purely by the order the author wrote them in. It takes
  // that body's level too: ownership is not a label over a shared plane.
  bool contested = false;
  for (int z = 0; z <= field.cellsZ; ++z)
    for (int x = 0; x <= field.cellsX; ++x) {
      if (field.height(x, z) >= -1.F)
        continue;
      const auto sample = lowHigh.sampleAt(x, z);
      assert(lowHigh.wet[sample] && highLow.wet[sample]);
      contested = true;
      // Body indices are positions in the authoring document, so the winner is
      // named by which body sits second in the order the author wrote.
      assert(lowThenHigh.bodies[lowHigh.body[sample]].id == "deep");
      assert(highThenLow.bodies[highLow.body[sample]].id == "shallow");
      assert(std::fabs(lowHigh.level[sample] - 0.5F) < 1e-5F);
      assert(std::fabs(highLow.level[sample] + 1.F) < 1e-5F);
    }
  assert(contested);

  // Every sample either body reaches is owned by the body the order says, at
  // that body's own level. Resolving a body index and then reading a level off
  // a different body is the mistake this catches: ownership is not a label over
  // a shared plane.
  auto levelOf = [](const std::string &id) {
    return id == "deep" ? 0.5F : -1.F;
  };
  for (int z = 0; z <= field.cellsZ; ++z)
    for (int x = 0; x <= field.cellsX; ++x) {
      const auto sample = lowHigh.sampleAt(x, z);
      if (!lowHigh.wet[sample] || !highLow.wet[sample])
        continue;
      const auto first = lowThenHigh.bodies[lowHigh.body[sample]].id;
      const auto second = highThenLow.bodies[highLow.body[sample]].id;
      assert(std::fabs(lowHigh.level[sample] - levelOf(first)) < 1e-5F);
      assert(std::fabs(highLow.level[sample] - levelOf(second)) < 1e-5F);
    }

  // The deep body reaches further out than the shallow one. That band is not
  // contested, so both orderings name the same body there at the same level:
  // priority decides an overlap, it does not make the result order-dependent.
  bool onlyDeep = false;
  for (int z = 0; z <= field.cellsZ; ++z)
    for (int x = 0; x <= field.cellsX; ++x) {
      if (field.height(x, z) <= -1.F || field.height(x, z) > 0.5F)
        continue;
      const auto sample = lowHigh.sampleAt(x, z);
      if (!lowHigh.wet[sample] || lowHigh.level[sample] != 0.5F)
        continue;
      onlyDeep = true;
      assert(lowThenHigh.bodies[lowHigh.body[sample]].id == "deep");
      assert(highThenLow.bodies[highLow.body[sample]].id == "deep");
    }
  assert(onlyDeep);
}

// Rejected bodies are counted, not silently ignored, and they never take the
// rest of the document down with them.
void invalidBodiesAreDroppedAndCounted() {
  const auto field = bowlField();
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  authoring.bodies.push_back(lake("", 0, 5));     // no stable id
  authoring.bodies.push_back(lake("tarn", 0, 5)); // accepted
  authoring.bodies.push_back(lake("tarn", 1, 5)); // duplicate id
  auto broken = lake("broken", 0, 5);
  broken.level = std::numeric_limits<float>::quiet_NaN();
  authoring.bodies.push_back(broken); // non-finite level
  auto stub = lake("stub", 0, 5);
  stub.kind = TerrainWaterBody::River;
  stub.riverPath = {Vec3{2.F, 0.F, 2.F}}; // a river needs a path
  stub.riverWidth = 2;
  authoring.bodies.push_back(stub);

  const auto result = *carveTerrainWater(field, authoring, nullptr);
  assert(result.dropped == 4);
  assert(result.surfaces.size() == 1);
  assert(result.surfaces.front().id == "tarn");

  // A duplicate id is detected across the whole document, so a body rejected
  // for another reason still reserves its id. An author who wrote the same name
  // twice gets both problems reported instead of one silently repaired.
  TerrainWaterAuthoring shadowed;
  shadowed.authored = true;
  auto nan = lake("shared", 0, 5);
  nan.level = std::numeric_limits<float>::quiet_NaN();
  auto again = lake("shared", 1, 5);
  shadowed.bodies.push_back(nan);
  shadowed.bodies.push_back(again);
  assert(carveTerrainWater(field, shadowed, nullptr)->dropped == 2);

  // Nothing authored means nothing to do, which is not the same as a field that
  // failed to carve.
  TerrainWaterAuthoring empty;
  const auto none = carveTerrainWater(field, empty, nullptr);
  assert(!none);
}

void invalidAppearanceRejectsTheBodyAndReservesItsId() {
  const auto field = bowlField();
  auto broken = lake("broken", 0, 5);
  broken.appearance.absorptionDistance = 0.F;
  auto duplicate = lake("broken", 0, 5);
  auto valid = lake("valid", 0, 5);
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  authoring.bodies = {broken, duplicate, valid};
  const auto accepted = terrain_water_detail::acceptedBodies(authoring);
  assert(accepted == std::vector<std::size_t>{2});
  const auto result =
      *refreshTerrainWaterResult(field, authoring, field.heights);
  assert(result.dropped == 2);
  assert(result.surfaces.size() == 1 && result.surfaces[0].id == "valid");
}

// An ocean with no radius is the sea: it fills every basin the landform left
// below sea level, and stops at the ridge.
void oceanFillsEveryBasinBelowSeaLevel() {
  // Two basins with a ridge between them, both below sea level.
  const auto field = fieldWith({16, 16}, 16, 16, [](double x, double z) {
    const auto left = std::hypot(x - 4, z - 8);
    const auto right = std::hypot(x - 12, z - 8);
    return float(std::min(left, right) - 3.5);
  });
  TerrainWaterBodySpec sea;
  sea.id = "sea";
  sea.kind = TerrainWaterBody::Ocean;
  sea.level = 0;
  sea.radius = 0; // every basin below the level
  const auto result = *carveTerrainWater(field, authoringWith(sea), nullptr);
  assert(result.surfaces.size() == 1);
  const auto &surface = result.surfaces.front();
  assert(surface.kind == TerrainWaterBody::Ocean);
  assert(!surface.vertices.empty());

  const auto field_view = terrain_water_detail::buildWaterLevelField(
      field.size, field.cellsX, field.cellsZ, result.carvedHeights,
      authoringWith(sea));
  // Both basins are water, and the ridge between them is not.
  assert(field_view.wet[field_view.sampleAt(4, 8)]);
  assert(field_view.wet[field_view.sampleAt(12, 8)]);
  assert(!field_view.wet[field_view.sampleAt(8, 8)]);
  // A sample high on the rim stays dry even though the ocean has no radius: the
  // level, not a disc, is what bounds the sea.
  assert(!field_view.wet[field_view.sampleAt(0, 0)]);
  // The rendered surface reaches both basins, so the mesh is not a single
  // puddle.
  auto reachedLeft = false;
  auto reachedRight = false;
  for (const auto &vertex : surface.vertices) {
    reachedLeft = reachedLeft || vertex.x < 8.F;
    reachedRight = reachedRight || vertex.x > 8.F;
  }
  assert(reachedLeft && reachedRight);
}

// Bodies the document declares but cannot carve are counted. Every accepted
// body gets a surface entry, so a renderer can tell "nothing to draw" from
// "never authored".
void everyAcceptedBodyGetsASurface() {
  const auto field = bowlField();
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  // Far apart, so neither shadows the other.
  auto first = lake("west", 0, 2);
  first.center = {2, 2};
  auto second = lake("east", 0, 2);
  second.center = {6, 6};
  // A body authored above the ground around it is accepted and draws nothing,
  // which is a different statement from never having been authored.
  auto dry = lake("dry", 0, 2);
  dry.center = {0, 0};
  authoring.bodies.push_back(first);
  authoring.bodies.push_back(second);
  authoring.bodies.push_back(dry);

  const auto result = *carveTerrainWater(field, authoring, nullptr);
  assert(result.dropped == 0);
  assert(result.surfaces.size() == 3);
  assert(result.surfaces[0].id == "west" &&
         !result.surfaces[0].vertices.empty());
  assert(result.surfaces[1].id == "east" &&
         !result.surfaces[1].vertices.empty());
  assert(result.surfaces[2].id == "dry" && result.surfaces[2].vertices.empty());

  // A bounded body stays local: the mesh may reach one cell past its waterline
  // so the shore is not stepped along cell boundaries, but it never bleeds into
  // the other end of the field.
  for (const auto &vertex : result.surfaces[0].vertices)
    assert(std::hypot(vertex.x - 2, vertex.z - 2) <= 4.F);
  for (const auto &vertex : result.surfaces[1].vertices)
    assert(std::hypot(vertex.x - 6, vertex.z - 6) <= 4.F);
}

// Graph stages carve only their newly authored body. Rebuilding the final
// surfaces from all authored bodies must keep the earlier one and read the
// already-carved ground exactly, without running either carve again.
void refreshedSurfacesComposeWithoutRecarving() {
  const auto field = bowlField();
  auto west = lake("west", 0, 4);
  west.center = {2, 4};
  auto east = lake("east", 0, 4);
  east.center = {6, 4};
  const auto first = *carveTerrainWater(field, authoringWith(west), nullptr);
  HeightField afterWest = field;
  afterWest.heights = first.carvedHeights;
  const auto second =
      *carveTerrainWater(afterWest, authoringWith(east), nullptr);

  TerrainWaterAuthoring combined;
  combined.authored = true;
  combined.bodies = {west, east};
  auto supplied = second.carvedHeights;
  supplied.set(field.index(4, 4), -7.F);
  // Only the grid shape is read from this argument. It may have no heights;
  // the explicit sample buffer is the final bed, not a cue to carve anew.
  HeightField grid = field;
  grid.heights.clear();
  const auto refreshed = *refreshTerrainWaterResult(grid, combined, supplied);
  assert(refreshed.carvedHeights == supplied);
  assert(refreshed.carvedHeights[field.index(4, 4)] == -7.F);
  assert(first.carvedHeights != refreshed.carvedHeights);
  assert(second.carvedHeights[field.index(4, 4)] != -7.F);
  assert(refreshed.dropped == 0 && refreshed.surfaces.size() == 2);
  assert(refreshed.surfaces[0].id == "west" &&
         !refreshed.surfaces[0].vertices.empty());
  assert(refreshed.surfaces[1].id == "east" &&
         !refreshed.surfaces[1].vertices.empty());

  const auto ownership = terrain_water_detail::buildWaterLevelField(
      field.size, field.cellsX, field.cellsZ, refreshed.carvedHeights,
      combined);
  assert(ownership.body[ownership.sampleAt(4, 4)] == 1);
  const auto &eastSurface = refreshed.surfaces[1];
  bool foundCenter = false;
  for (std::size_t vertex = 0; vertex < eastSurface.vertices.size(); ++vertex) {
    if (eastSurface.vertices[vertex].x == 4.F &&
        eastSurface.vertices[vertex].z == 4.F) {
      assert(eastSurface.depth[vertex] == 7.F);
      foundCenter = true;
    }
  }
  assert(foundCenter);

  combined.bodies.push_back(west); // Duplicate stable ID is counted, not drawn.
  const auto rejected = *refreshTerrainWaterResult(grid, combined, supplied);
  assert(rejected.dropped == 1 && rejected.surfaces.size() == 2);
  assert(rejected.carvedHeights == supplied);
  TerrainSamples<float> wrongCount;
  wrongCount.resize(4);
  assert(!refreshTerrainWaterResult(grid, combined, wrongCount));
  assert(!refreshTerrainWaterResult(grid, TerrainWaterAuthoring{}, supplied));
  std::stop_source cancelled;
  cancelled.request_stop();
  assert(!refreshTerrainWaterResult(grid, combined, supplied,
                                    cancelled.get_token()));
}

void partialCellsReachTheActualShoreWithoutDryCorners() {
  // No cell has four wet corners. Restricting output to full cells loses all
  // water; accepting any wet corner draws twice the intended area.
  const auto field =
      fieldWith({1, 2}, 1, 2, [](double x, double) { return float(x - 0.75); });
  auto sea = lake("sea", 0);
  sea.kind = TerrainWaterBody::Ocean;
  sea.shorelineSoftening = false;
  const auto authoring = authoringWith(sea);
  const auto result =
      *refreshTerrainWaterResult(field, authoring, field.heights);
  const auto &surface = result.surfaces.front();
  assert(std::fabs(surfaceArea(surface) - 1.5) < 1e-6);
  bool sharedEdge = false;
  bool diagonal = false;
  for (std::size_t i = 0; i < surface.vertices.size(); ++i) {
    const auto point = surface.vertices[i];
    assert(point.x <= 0.75F);
    assert(std::fabs(surface.depth[i] - (0.75F - point.x)) < 1e-6F);
    sharedEdge = sharedEdge || (point.x == 0.75F && point.z == 1.F);
    diagonal = diagonal || (point.x == 0.75F && point.z == 0.25F);
  }
  assert(sharedEdge && diagonal);
  // Shared crossings are reused by triangles on both sides of the cell edge.
  for (std::size_t i = 0; i < surface.vertices.size(); ++i) {
    const auto point = surface.vertices[i];
    if (point.x != 0.75F || point.z != 1.F)
      continue;
    bool below = false;
    bool above = false;
    for (std::size_t t = 0; t < surface.indices.size(); t += 3) {
      if (surface.indices[t] != i && surface.indices[t + 1] != i &&
          surface.indices[t + 2] != i)
        continue;
      for (std::size_t c = 0; c < 3; ++c) {
        const auto z = surface.vertices[surface.indices[t + c]].z;
        below = below || z < 1.F;
        above = above || z > 1.F;
      }
    }
    assert(below && above);
  }
  const auto again =
      *refreshTerrainWaterResult(field, authoring, field.heights);
  assert(again.surfaces.front().indices == surface.indices);
  assert(again.surfaces.front().depth == surface.depth);
}

void ownershipClipsBodiesRatherThanSharingCells() {
  const auto field =
      fieldWith({1, 1}, 1, 1, [](double, double) { return -1.F; });
  auto first = lake("first", 2);
  auto later = lake("later", 0, 0.1F);
  later.center = {1, 1};
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  authoring.bodies = {first, later};
  const auto result =
      *refreshTerrainWaterResult(field, authoring, field.heights);
  assert(std::fabs(surfaceArea(result.surfaces[0]) - 0.875) < 1e-6);
  assert(std::fabs(surfaceArea(result.surfaces[1]) - 0.125) < 1e-6);
  for (const auto point : result.surfaces[0].vertices)
    assert(point.x + point.z <= 1.5F);
  for (const auto point : result.surfaces[1].vertices)
    assert(point.x + point.z >= 1.5F);

  authoring.bodies[1].radius = 0;
  const auto shadowed =
      *refreshTerrainWaterResult(field, authoring, field.heights);
  assert(shadowed.surfaces[0].vertices.empty());
  assert(shadowed.surfaces[0].indices.empty());
  assert(surfaceArea(shadowed.surfaces[1]) == 1.0);
}

void zeroDepthDoesNotEmitDegenerateTrianglesOrInventBanks() {
  const auto field =
      fieldWith({1, 1}, 1, 1, [](double, double) { return 0.F; });
  auto body = lake("bounded", 0, 0.1F);
  body.center = {0, 0};
  auto authoring = authoringWith(body);
  const auto dry = *refreshTerrainWaterResult(field, authoring, field.heights);
  assert(dry.surfaces.front().vertices.empty());
  assert(dry.surfaces.front().indices.empty());
  authoring.bodies[0].level = 5.F;
  const auto raised =
      *refreshTerrainWaterResult(field, authoring, field.heights);
  assert(raised.carvedHeights == field.heights);
  assert(raised.surfaces.front().level == 5.F);
  assert(surfaceArea(raised.surfaces.front()) == 0.125);
  for (const auto depth : raised.surfaces.front().depth)
    assert(depth == 5.F);

  // Two vertices on the level and the third above it leave only a line.
  const auto line =
      fieldWith({1, 1}, 1, 1, [](double x, double z) { return float(x * z); });
  authoring.bodies[0].radius = 0;
  authoring.bodies[0].level = 0;
  const auto touching =
      *refreshTerrainWaterResult(line, authoring, line.heights);
  assert(touching.surfaces.front().indices.empty());

  std::stop_source cancelled;
  cancelled.request_stop();
  assert(!carveTerrainWater(field, authoring, nullptr, cancelled.get_token()));
  assert(terrain_water_detail::buildWaterGeometryField(
             field.size, field.cellsX, field.cellsZ, field.heights, authoring,
             cancelled.get_token())
             .samples() == 0);
}

void sharedClippingHandlesBothBoundariesAndCanonicalEdges() {
  using namespace terrain_water_detail;
  const WaterGeometryVertex a{0, 0, 3, -1};
  const WaterGeometryVertex c{0, 1, -1, 1};
  const WaterGeometryVertex b{1, 0, 3, 1};
  const auto polygon = clipWaterTriangle({a, c, b});
  assert(polygon.count == 5);
  for (std::size_t i = 0; i < polygon.count; ++i) {
    const auto &point = polygon.vertices[i];
    assert(point.depth >= 0 && point.coverage >= 0);
    assert(
        !sameWaterPosition(point, polygon.vertices[(i + 1) % polygon.count]));
  }
  const auto forward =
      waterBoundaryIntersection(a, c, &WaterGeometryVertex::depth);
  const auto reverse =
      waterBoundaryIntersection(c, a, &WaterGeometryVertex::depth);
  assert(sameWaterPosition(forward, reverse));
  assert(forward.depth == 0 && forward.coverage == reverse.coverage);
  const auto maskForward =
      waterBoundaryIntersection(a, b, &WaterGeometryVertex::coverage);
  const auto maskReverse =
      waterBoundaryIntersection(b, a, &WaterGeometryVertex::coverage);
  assert(sameWaterPosition(maskForward, maskReverse));
  assert(maskForward.coverage == 0 && maskForward.depth == maskReverse.depth);
  const auto touching = clipWaterTriangle({WaterGeometryVertex{0, 0, 0, 1},
                                           WaterGeometryVertex{0, 1, -1, 1},
                                           WaterGeometryVertex{1, 0, 1, 1}});
  assert(touching.count == 3);
}

// A field that is not a usable grid is reported, not carved into.
void unusableFieldsAreReported() {
  const auto field = bowlField();
  auto authoring = authoringWith(lake("tarn", 0, 5));
  assert(!carveTerrainWater(field, TerrainWaterAuthoring{}, nullptr));
  // A non-finite declared sea level is a corrupt document, which is a different
  // failure from a body being wrong and must not be counted as a dropped body.
  auto corrupt = authoring;
  corrupt.seaLevel = std::numeric_limits<float>::infinity();
  assert(!carveTerrainWater(field, corrupt, nullptr));

  HeightField empty = field;
  empty.cellsX = 0;
  assert(!carveTerrainWater(empty, authoring, nullptr));
  HeightField mismatched = field;
  mismatched.heights.resize(4);
  assert(!carveTerrainWater(mismatched, authoring, nullptr));
}
} // namespace

void lakeSelectsOnlyItsConnectedBasin() {
  const auto makeGround = [](bool channel) {
    return fieldWith({12, 8}, 12, 8, [channel](double x, double z) {
      if ((x >= 1 && x <= 4 && z >= 2 && z <= 6) ||
          (x >= 8 && x <= 11 && z >= 2 && z <= 6) ||
          (channel && x >= 4 && x <= 8 && z == 4))
        return 0.F;
      return 4.F;
    });
  };
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  TerrainWaterBodySpec lake;
  lake.id = "seeded_lake";
  lake.kind = TerrainWaterBody::Lake;
  lake.center = {2, 4};
  lake.level = 2;
  lake.radius = 20;
  lake.shorelineSoftening = false;
  authoring.bodies.push_back(lake);
  auto ground = makeGround(false);
  auto isolated = carveTerrainWater(ground, authoring, nullptr);
  assert(isolated && isolated->resolvedCoverage);
  assert(isolated->carvedHeights[ground.index(9, 4)] == 0);
  for (const auto vertex : isolated->surfaces.front().vertices)
    assert(vertex.x < 6);
  auto query = TerrainWaterQueryContext::build(authoring, *isolated, nullptr,
                                               12, 8, {12, 8});
  assert(query && query->sample({2, 0, 4})->submerged);
  assert(!query->sample({9, 0, 4})->submerged);
  // Lowering a connecting channel recomputes coverage once; the second basin
  // becomes part of the same body in rendering and queries.
  auto connected = carveTerrainWater(makeGround(true), authoring, nullptr);
  auto connectedQuery = TerrainWaterQueryContext::build(
      authoring, *connected, nullptr, 12, 8, {12, 8});
  assert(connectedQuery && connectedQuery->sample({9, 0, 4})->submerged);
  // Oceans intentionally cover independent depressions at their level.
  authoring.bodies.front().kind = TerrainWaterBody::Ocean;
  auto ocean = carveTerrainWater(ground, authoring, nullptr);
  auto oceanQuery = TerrainWaterQueryContext::build(authoring, *ocean, nullptr,
                                                    12, 8, {12, 8});
  assert(oceanQuery && oceanQuery->sample({9, 0, 4})->submerged);
  std::stop_source cancel;
  cancel.request_stop();
  assert(!carveTerrainWater(ground, authoring, nullptr, cancel.get_token()));
}

int main() {
  riverCarvesChannelAndLeavesFieldUnchanged();
  riverDepthFollowsDrainage();
  lakeFillsBasinWithLevelSurface();
  surfaceDepthRampsToZeroAtTheEdge();
  shorelineSofteningLowersGroundNearTheWaterline();
  bodyOrderDecidesOverlap();
  invalidBodiesAreDroppedAndCounted();
  invalidAppearanceRejectsTheBodyAndReservesItsId();
  oceanFillsEveryBasinBelowSeaLevel();
  everyAcceptedBodyGetsASurface();
  refreshedSurfacesComposeWithoutRecarving();
  partialCellsReachTheActualShoreWithoutDryCorners();
  ownershipClipsBodiesRatherThanSharingCells();
  zeroDepthDoesNotEmitDegenerateTrianglesOrInventBanks();
  sharedClippingHandlesBothBoundariesAndCanonicalEdges();
  unusableFieldsAreReported();
  lakeSelectsOnlyItsConnectedBasin();
  std::cout << "Terrain water checks passed\n";
}
