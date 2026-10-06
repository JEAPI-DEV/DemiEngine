#include "demi/runtime/terrain/TerrainWaterQueries.h"

#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainMasks.h"

#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace demi::runtime;

namespace {

// A field built from a height function, so each test states the ground it means
// to ask about. The answers below are exact numbers derived from these
// functions, which is the only way a gameplay query can be asserted at all.
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

// A plane falling one metre every four units. The waterline at level 0 is
// therefore the line z == 16, and any query's expected depth is readable off
// the function instead of being measured back out of the engine.
float rampHeight(double, double z) { return float(4.0 - 0.25 * z); }
HeightField rampField() { return fieldWith({32, 32}, 32, 32, rampHeight); }

// A trench along z == 16, for a river that has somewhere to run.
float trenchHeight(double, double z) {
  const auto reach = std::max(0.0, 1.0 - std::fabs(z - 16.0) / 4.0);
  return float(2.0 - 4.0 * reach);
}
HeightField trenchField() { return fieldWith({32, 32}, 32, 32, trenchHeight); }

TerrainWaterBodySpec ocean(std::string id, float level) {
  TerrainWaterBodySpec spec;
  spec.id = std::move(id);
  spec.kind = TerrainWaterBody::Ocean;
  spec.level = level;
  return spec;
}

TerrainWaterBodySpec lake(std::string id, float level, Vec2 center,
                          float radius) {
  TerrainWaterBodySpec spec;
  spec.id = std::move(id);
  spec.kind = TerrainWaterBody::Lake;
  spec.level = level;
  spec.center = center;
  spec.radius = radius;
  return spec;
}

TerrainWaterAuthoring authoringWith(TerrainWaterBodySpec body) {
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  authoring.bodies.push_back(std::move(body));
  return authoring;
}

TerrainMasks flowingField(const HeightField &field, float flow) {
  TerrainMasks masks;
  masks.flow.resize(field.heights.size());
  masks.flow.assign(field.heights.size(), flow);
  return masks;
}

std::optional<TerrainWaterQuery> ask(const TerrainWaterAuthoring &authoring,
                                     const TerrainWaterResult &result,
                                     const TerrainMasks *masks, Vec3 at,
                                     const HeightField &field) {
  return sampleTerrainWaterAt(authoring, result, masks, at, field.cellsX,
                              field.cellsZ, field.size);
}

// Dry land is dry: not submerged, no depth, and no body claiming a point that
// has no water in it.
void dryPointIsNotSubmerged() {
  const auto field = rampField();
  const auto authoring = authoringWith(ocean("sea", 0));
  const auto result = *carveTerrainWater(field, authoring, nullptr);

  const auto high = *ask(authoring, result, nullptr, Vec3{16, 5, 4}, field);
  assert(!high.submerged);
  assert(high.depth == 0.F);
  assert(high.bodyId.empty());
  assert(!high.swimmable);
  assert(high.flowSpeed == 0.F);

  // A caller that queries above the ground and one that queries deep below it
  // both get an answer, because the query is about water, not about the terrain
  // surface. The ground is only used to decide whether water is there.
  const auto buried = *ask(authoring, result, nullptr, Vec3{16, 500, 4}, field);
  assert(!buried.submerged && buried.depth == 0.F);
}

// Under water, the answer names the body and the depth is the distance from the
// plane down to the carved ground.
void submergedPointReportsDepthAndBody() {
  const auto field = rampField();
  const auto authoring = authoringWith(ocean("sea", 0));
  const auto result = *carveTerrainWater(field, authoring, nullptr);

  // The ramp is 4 - 0.25z and the sea is level 0, so the fill at z is 0.25z -
  // 16 once the ground is below the level.
  const auto deep = *ask(authoring, result, nullptr, Vec3{16, 0, 28}, field);
  assert(deep.submerged);
  assert(std::fabs(deep.depth - 3.F) < 1e-4F);
  assert(deep.bodyId == "sea");
  assert(deep.swimmable);
  // The sea is still water by definition, whatever the drainage field says.
  assert(deep.flowSpeed == 0.F);

  // Shallower than the swim depth is a wade, not a swim.
  const auto wading = *ask(authoring, result, nullptr, Vec3{16, 0, 18}, field);
  assert(wading.submerged);
  assert(std::fabs(wading.depth - 0.5F) < 1e-4F);
  assert(!wading.swimmable);

  // Triangulated on the authored grid, so a point between samples is answered
  // from the surface it is really over rather than from a snapped sample.
  const auto between =
      *ask(authoring, result, nullptr, Vec3{16, 0, 16.5}, field);
  assert(between.submerged);
  assert(std::fabs(between.depth - 0.125F) < 1e-4F);
}

// The waterline itself is neither wet nor dry, and just inside it the depth is
// as small as the terrain says. A shoreline that reported a body's depth, or
// none at all, would misplace foam and buoyancy.
void shorelineIsNeitherWetNorDry() {
  const auto field = rampField();
  const auto authoring = authoringWith(ocean("sea", 0));
  const auto result = *carveTerrainWater(field, authoring, nullptr);

  // Exactly on the line the ground meets the plane: dry, with no depth.
  const auto onLine = *ask(authoring, result, nullptr, Vec3{16, 0, 16}, field);
  assert(!onLine.submerged);
  assert(onLine.depth == 0.F);
  assert(onLine.bodyId.empty());

  // A tenth of a unit inside, the water is exactly that shallow.
  const auto inside =
      *ask(authoring, result, nullptr, Vec3{16, 0, 16.1F}, field);
  assert(inside.submerged);
  assert(inside.depth > 0.F && inside.depth < 0.05F);
  assert(inside.bodyId == "sea");
  assert(!inside.swimmable);

  // A tenth of a unit outside there is still dry, so the waterline is a place
  // rather than a step at a sample boundary.
  const auto outside =
      *ask(authoring, result, nullptr, Vec3{16, 0, 15.9F}, field);
  assert(!outside.submerged);
}

// A point outside the field is not a point at its edge. Clamping would let a
// body of water that does not exist under the caller answer for it.
void outsideTheFieldIsReportedNotClamped() {
  const auto field = rampField();
  const auto authoring = authoringWith(ocean("sea", 0));
  const auto result = *carveTerrainWater(field, authoring, nullptr);

  assert(!ask(authoring, result, nullptr, Vec3{-1, 0, 16}, field));
  assert(!ask(authoring, result, nullptr, Vec3{16, 0, -0.5F}, field));
  assert(!ask(authoring, result, nullptr, Vec3{33, 0, 16}, field));
  assert(!ask(authoring, result, nullptr, Vec3{16, 0, 32.5F}, field));
  const auto notANumber = std::numeric_limits<float>::quiet_NaN();
  assert(!ask(authoring, result, nullptr, Vec3{notANumber, 0, 16}, field));
  assert(!ask(authoring, result, nullptr, Vec3{16, notANumber, 16}, field));

  // The field's own corners are inside it and are answered.
  const auto corner = *ask(authoring, result, nullptr, Vec3{0, 0, 32}, field);
  assert(corner.submerged && corner.bodyId == "sea");

  // A grid that does not match the carved heights is a caller error, reported
  // rather than resampled: quietly answering from another resolution is how
  // gameplay and visuals start disagreeing.
  assert(!sampleTerrainWaterAt(authoring, result, nullptr, Vec3{16, 0, 28}, 16,
                               16, field.size));
  assert(!sampleTerrainWaterAt(authoring, result, nullptr, Vec3{16, 0, 28},
                               field.cellsX, field.cellsZ, Vec2{0, 32}));
  assert(!sampleTerrainWaterAt(TerrainWaterAuthoring{}, result, nullptr,
                               Vec3{16, 0, 28}, field.cellsX, field.cellsZ,
                               field.size));
}

// Flow comes from the drainage field and only a river has any. A lake fed by a
// river is still where it stands, and a project with no drainage stage has
// rivers that are not reported as moving.
void flowIsReportedForRiversOnly() {
  const auto field = trenchField();
  TerrainWaterAuthoring riverAuthoring;
  riverAuthoring.authored = true;
  TerrainWaterBodySpec river;
  river.id = "brook";
  river.kind = TerrainWaterBody::River;
  river.level = 0;
  river.riverWidth = 5;
  river.riverPath = {Vec3{2.F, -3.5F, 16.F}, Vec3{30.F, -3.5F, 16.F}};
  riverAuthoring.bodies.push_back(river);
  const auto riverResult = *carveTerrainWater(field, riverAuthoring, nullptr);
  const auto flowing = flowingField(field, 1.F);

  const auto inRiver =
      *ask(riverAuthoring, riverResult, &flowing, Vec3{16, 0, 16}, field);
  assert(inRiver.submerged);
  assert(inRiver.bodyId == "brook");
  assert(inRiver.flowSpeed > 0.F);
  assert(std::fabs(inRiver.flowSpeed - terrainWaterReferenceFlowSpeed) < 1e-3F);
  // A channel this fast is a rapid: deep enough, and not somewhere to swim.
  assert(inRiver.depth >= terrainWaterMinSwimDepth);
  assert(!inRiver.swimmable);

  // A quarter of the accumulation is a quarter of the reported speed, so the
  // answer tracks the field rather than saturating at the first bit of
  // drainage.
  const auto quarter = flowingField(field, 0.25F);
  const auto slower =
      *ask(riverAuthoring, riverResult, &quarter, Vec3{16, 0, 16}, field);
  assert(slower.flowSpeed > 0.F);
  assert(std::fabs(slower.flowSpeed - inRiver.flowSpeed * 0.25F) < 1e-3F);
  // Slow enough now to swim, at the same depth: only the flow changed.
  assert(slower.swimmable);

  // No drainage data: the river is still water as far as gameplay can tell,
  // which is a documented consequence of authoring without a drainage stage.
  const auto unmeasured =
      *ask(riverAuthoring, riverResult, nullptr, Vec3{16, 0, 16}, field);
  assert(unmeasured.submerged && unmeasured.bodyId == "brook");
  assert(unmeasured.flowSpeed == 0.F);

  // The same point under the same drainage, as a lake: still, and swimmable.
  const auto lakeAuthoring = authoringWith(lake("pool", 0, {16, 16}, 3));
  const auto lakeResult = *carveTerrainWater(field, lakeAuthoring, &flowing);
  const auto inLake =
      *ask(lakeAuthoring, lakeResult, &flowing, Vec3{16, 0, 16}, field);
  assert(inLake.submerged);
  assert(inLake.bodyId == "pool");
  assert(inLake.flowSpeed == 0.F);
  assert(inLake.swimmable);
  // The lake still deepens its own basin, and the depth reflects the cut.
  assert(inLake.depth > 2.F);

  // And an ocean over the same ground, under the same drainage.
  const auto seaAuthoring = authoringWith(ocean("sea", 0));
  const auto seaResult = *carveTerrainWater(field, seaAuthoring, &flowing);
  const auto inSea =
      *ask(seaAuthoring, seaResult, &flowing, Vec3{16, 0, 16}, field);
  assert(inSea.flowSpeed == 0.F);
}

// The whole reason the query is not answered from the mesh. A water surface
// that has been culled, coarsened or dropped must leave every gameplay answer
// exactly where it was, or a distant river disappears from a character standing
// in it.
void gameplayAnswersIgnoreTheRenderSurface() {
  const auto field = rampField();
  const auto authoring = authoringWith(ocean("sea", 0));
  const auto full = *carveTerrainWater(field, authoring, nullptr);
  assert(!full.surfaces.empty());
  assert(!full.surfaces.front().vertices.empty());

  // The same carved terrain, with the surfaces thrown away: what a culled or
  // beyond-the-horizon water body looks like to the game.
  auto culled = full;
  for (auto &surface : culled.surfaces) {
    surface.vertices.clear();
    surface.indices.clear();
    surface.normals.clear();
    surface.depth.clear();
  }
  // And a surface that is not the shape anyone would render: one triangle in
  // the wrong place, carrying depths that are nothing like the bathymetry.
  auto wrong = full;
  wrong.surfaces.front().vertices = {Vec3{-50, 900, -50}, Vec3{-49, 900, -50},
                                     Vec3{-50, 900, -49}};
  wrong.surfaces.front().indices = {0, 1, 2};
  wrong.surfaces.front().normals = {Vec3{0, 0, 1}, Vec3{0, 0, 1},
                                    Vec3{0, 0, 1}};
  wrong.surfaces.front().depth = {100, 100, 100};

  for (const auto at : {Vec3{16, 0, 4}, Vec3{16, 0, 16}, Vec3{16, 0, 16.1F},
                        Vec3{16, 0, 20}, Vec3{0, 0, 32}, Vec3{31, 0, 24}}) {
    const auto reference = *ask(authoring, full, nullptr, at, field);
    const auto withoutMesh = *ask(authoring, culled, nullptr, at, field);
    const auto withWrongMesh = *ask(authoring, wrong, nullptr, at, field);
    assert(reference.submerged == withoutMesh.submerged);
    assert(reference.submerged == withWrongMesh.submerged);
    assert(reference.depth == withoutMesh.depth);
    assert(reference.depth == withWrongMesh.depth);
    assert(reference.bodyId == withoutMesh.bodyId);
    assert(reference.bodyId == withWrongMesh.bodyId);
    assert(reference.swimmable == withoutMesh.swimmable);
    assert(reference.swimmable == withWrongMesh.swimmable);
    assert(reference.flowSpeed == withoutMesh.flowSpeed);
    assert(reference.flowSpeed == withWrongMesh.flowSpeed);
  }
  // The differences really were differences, or the comparison above proves
  // nothing.
  assert(wrong.surfaces.front().depth.front() == 100.F);
  assert(full.surfaces.front().depth.front() != 100.F);
}

// A context answers every point the one-shot call does. Rebuilding the level
// plane per query would be quadratic in a per-frame loop, and the two paths
// existing at all is only safe if they cannot disagree.
void contextMatchesTheOneShotCall() {
  const auto field = rampField();
  const auto authoring = authoringWith(ocean("sea", 0));
  const auto result = *carveTerrainWater(field, authoring, nullptr);
  const auto flowing = flowingField(field, 0.25F);
  const auto context = TerrainWaterQueryContext::build(
      authoring, result, &flowing, field.cellsX, field.cellsZ, field.size);
  assert(context);
  assert(context->cellsX() == field.cellsX &&
         context->cellsZ() == field.cellsZ);
  assert(context->size().x == field.size.x &&
         context->size().y == field.size.y);

  for (int z = 0; z <= field.cellsZ; z += 2)
    for (int x = 0; x <= field.cellsX; x += 2) {
      const Vec3 at{float(x), 0, float(z)};
      const auto once = *ask(authoring, result, &flowing, at, field);
      const auto held = *context->sample(at);
      assert(once.submerged == held.submerged);
      assert(once.depth == held.depth);
      assert(once.bodyId == held.bodyId);
      assert(once.swimmable == held.swimmable);
    }
  // The same bounds rule as the one-shot call, and a context that was never
  // built answers nothing rather than something invented.
  assert(!context->sample(Vec3{-1, 0, 16}));
  assert(!TerrainWaterQueryContext::build(authoring, result, nullptr, 0, 8,
                                          field.size));
  assert(!TerrainWaterQueryContext::build(authoring, TerrainWaterResult{},
                                          nullptr, field.cellsX, field.cellsZ,
                                          field.size));
  // A drainage field on another grid is refused rather than resampled, exactly
  // as the carve refuses it. Guessing which of its samples is which cell would
  // invent how hard a river pushes a character downstream.
  TerrainMasks wrongGrid;
  wrongGrid.flow.resize(field.heights.size() / 2);
  assert(!TerrainWaterQueryContext::build(
      authoring, result, &wrongGrid, field.cellsX, field.cellsZ, field.size));
  assert(!sampleTerrainWaterAt(authoring, result, &wrongGrid, Vec3{16, 0, 28},
                               field.cellsX, field.cellsZ, field.size));
}

// A body that was dropped at carve time is invisible to gameplay as well. A
// river that failed validation must not appear in a query with no mesh behind
// it, which is the one case where the two views could disagree for real.
void droppedBodiesAreInvisibleToQueries() {
  const auto field = rampField();
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  authoring.bodies.push_back(lake("pool", 0, {16, 16}, 4));
  // Written second, so it would win the overlap if it were accepted at all: an
  // answer naming "pool" is proof the dropped body is gone from the query path
  // and not merely hidden behind the valid one.
  authoring.bodies.push_back(lake("", 0, {16, 16}, 4));
  auto invalidAppearance = lake("invalid-appearance", 0, {16, 16}, 4);
  invalidAppearance.appearance.roughness = 2.F;
  authoring.bodies.push_back(invalidAppearance);
  const auto result = *carveTerrainWater(field, authoring, nullptr);
  assert(result.dropped == 2);
  assert(result.surfaces.size() == 1);
  assert(result.surfaces.front().id == "pool");

  const auto query = *ask(authoring, result, nullptr, Vec3{16, 0, 18}, field);
  assert(query.submerged);
  assert(query.bodyId == "pool");
}

void nonplanarGroundUsesTheRenderedDiagonal() {
  const auto field = fieldWith(
      {1, 1}, 1, 1, [](double x, double z) { return float(4.0 * x * z); });
  const auto authoring = authoringWith(ocean("sea", 1));
  const auto result =
      *refreshTerrainWaterResult(field, authoring, field.heights);
  const auto context = *TerrainWaterQueryContext::build(
      authoring, result, nullptr, field.cellsX, field.cellsZ, field.size);
  // a,c,b is flat at zero; b,c,d rises as 4*(u+v-1).
  const auto first = *context.sample({0.25F, 0, 0.25F});
  assert(first.submerged && first.depth == 1.F);
  const auto diagonal = *context.sample({0.5F, 0, 0.5F});
  assert(diagonal.submerged && diagonal.depth == 1.F);
  const auto second = *context.sample({0.6F, 0, 0.6F});
  assert(second.submerged && second.bodyId == "sea");
  assert(std::fabs(second.depth - 0.2F) < 1e-6F);
  assert(!context.sample({0.625F, 0, 0.625F})->submerged);
  assert(!context.sample({0.9F, 0, 0.9F})->submerged);
  assert(!context.sample({1, 0, 1})->submerged);

  auto noMesh = result;
  noMesh.surfaces.clear();
  const auto independent =
      *ask(authoring, noMesh, nullptr, {0.6F, 0, 0.6F}, field);
  assert(independent.depth == second.depth &&
         independent.bodyId == second.bodyId);
}

void partialCellQueriesFollowGroundAndFootprintClips() {
  const auto field =
      fieldWith({1, 2}, 1, 2, [](double x, double) { return float(x - 0.75); });
  const auto authoring = authoringWith(ocean("sea", 0));
  const auto result =
      *refreshTerrainWaterResult(field, authoring, field.heights);
  const auto context = *TerrainWaterQueryContext::build(
      authoring, result, nullptr, field.cellsX, field.cellsZ, field.size);
  for (const auto z : {0.F, 0.25F, 0.999F, 1.F, 1.001F, 2.F}) {
    const auto inside = *context.sample({0.7F, 0, z});
    assert(inside.submerged && inside.bodyId == "sea");
    assert(std::fabs(inside.depth - 0.05F) < 1e-6F);
    assert(!context.sample({0.75F, 0, z})->submerged);
    assert(!context.sample({0.8F, 0, z})->submerged);
    assert(!context.sample({1, 0, z})->submerged);
  }

  const auto flat =
      fieldWith({1, 1}, 1, 1, [](double, double) { return -1.F; });
  const auto bounded = authoringWith(lake("pool", 0, {0, 0}, 0.1F));
  const auto clipped = *refreshTerrainWaterResult(flat, bounded, flat.heights);
  const auto inside = *ask(bounded, clipped, nullptr, {0.1F, 0, 0.1F}, flat);
  assert(inside.submerged && inside.depth == 1.F && inside.bodyId == "pool");
  for (const auto point :
       {Vec3{0.4F, 0, 0.4F}, Vec3{0.5F, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 0, 1}}) {
    const auto dry = *ask(bounded, clipped, nullptr, point, flat);
    assert(!dry.submerged && dry.depth == 0 && dry.bodyId.empty());
  }
}

void ownershipSelectsOneAuthoredPlaneWithoutBlending() {
  const auto field =
      fieldWith({1, 1}, 1, 1, [](double, double) { return -1.F; });
  auto authoring = authoringWith(ocean("sea", 2));
  authoring.bodies.push_back(lake("pool", 0, {1, 1}, 0.1F));
  const auto result =
      *refreshTerrainWaterResult(field, authoring, field.heights);
  const auto context = *TerrainWaterQueryContext::build(
      authoring, result, nullptr, field.cellsX, field.cellsZ, field.size);
  const auto sea = *context.sample({0.6F, 0, 0.6F});
  assert(sea.submerged && sea.depth == 3.F && sea.bodyId == "sea");
  const auto pool = *context.sample({0.9F, 0, 0.9F});
  assert(pool.submerged && pool.depth == 1.F && pool.bodyId == "pool");
  const auto boundary = *context.sample({0.75F, 0, 0.75F});
  assert(!boundary.submerged && boundary.bodyId.empty());
  // The opposite ground triangle has no pool corner at all.
  const auto other = *context.sample({0.3F, 0, 0.3F});
  assert(other.depth == 3.F && other.bodyId == "sea");

  // A later bounded body whose plane is below the bed cannot steal wet water.
  authoring.bodies.back().level = -2.F;
  const auto dryLater =
      *refreshTerrainWaterResult(field, authoring, field.heights);
  const auto retained = *ask(authoring, dryLater, nullptr, {1, 0, 1}, field);
  assert(retained.depth == 3.F && retained.bodyId == "sea");
}

void drainageUsesTheSameTriangleWeights() {
  const auto field =
      fieldWith({1, 1}, 1, 1, [](double, double) { return -2.F; });
  auto river = ocean("river", 0);
  river.kind = TerrainWaterBody::River;
  river.riverWidth = 100;
  river.riverPath = {{-10, -2, 0.5F}, {10, -2, 0.5F}};
  const auto authoring = authoringWith(river);
  const auto result =
      *refreshTerrainWaterResult(field, authoring, field.heights);
  auto flow = flowingField(field, 0);
  flow.flow.set(field.index(1, 1), 1.F);
  const auto first = *ask(authoring, result, &flow, {0.25F, 0, 0.25F}, field);
  assert(first.flowSpeed == 0.F);
  const auto second = *ask(authoring, result, &flow, {0.6F, 0, 0.6F}, field);
  assert(second.submerged && second.bodyId == "river");
  assert(std::fabs(second.flowSpeed - 1.2F) < 1e-6F);
}
} // namespace

void contextSharesPreparedDataAndReportsPointContainment() {
  const auto field = rampField();
  const auto authoring = authoringWith(ocean("sea", 0));
  const auto result = *carveTerrainWater(field, authoring, nullptr);
  assert(result.resolvedCoverage);
  const auto references = result.resolvedCoverage.use_count();
  auto context = TerrainWaterQueryContext::build(
      authoring, result, nullptr, field.cellsX, field.cellsZ, field.size);
  assert(context && result.resolvedCoverage.use_count() == references + 1);
  const auto belowSurface = context->sample({16, -1, 28});
  assert(belowSurface && belowSurface->submerged && belowSurface->underwater);
  assert(belowSurface->surfaceHeight == 0);
  assert(std::fabs(belowSurface->groundHeight + 3) < 1e-4F);
  assert(!context->sample({16, 0, 28})->underwater);
  assert(!context->sample({16, 100, 28})->underwater);
  assert(!context->sample({16, -100, 28})->underwater);
  context.reset();
  assert(result.resolvedCoverage.use_count() == references);
}

void contextRetainsFlowSnapshotAfterCallerEdits() {
  const auto field = rampField();
  TerrainMasks masks;
  masks.flow.resize(field.heights.size());
  for (std::size_t sample = 0; sample < masks.flow.size(); ++sample)
    masks.flow.set(sample, 0.25F);
  auto river = ocean("river", 0);
  river.kind = TerrainWaterBody::River;
  river.riverPath = {{16, -4, 18}, {16, -4, 30}};
  river.riverWidth = 16;
  const auto authoring = authoringWith(river);
  const auto result = *carveTerrainWater(field, authoring, &masks);
  const auto context = TerrainWaterQueryContext::build(
      authoring, result, &masks, field.cellsX, field.cellsZ, field.size);
  assert(context);
  const auto before = context->sample({16, 0, 24});
  assert(before && before->submerged && before->bodyId == "river");
  for (std::size_t sample = 0; sample < masks.flow.size(); ++sample)
    masks.flow.set(sample, 1.F);
  const auto after = context->sample({16, 0, 24});
  assert(after && after->flowSpeed == before->flowSpeed);
}

int main() {
  dryPointIsNotSubmerged();
  submergedPointReportsDepthAndBody();
  shorelineIsNeitherWetNorDry();
  outsideTheFieldIsReportedNotClamped();
  flowIsReportedForRiversOnly();
  gameplayAnswersIgnoreTheRenderSurface();
  contextMatchesTheOneShotCall();
  droppedBodiesAreInvisibleToQueries();
  nonplanarGroundUsesTheRenderedDiagonal();
  partialCellQueriesFollowGroundAndFootprintClips();
  ownershipSelectsOneAuthoredPlaneWithoutBlending();
  drainageUsesTheSameTriangleWeights();
  contextSharesPreparedDataAndReportsPointContainment();
  contextRetainsFlowSnapshotAfterCallerEdits();
  std::cout << "Terrain water query checks passed\n";
}
