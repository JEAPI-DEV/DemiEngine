#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainLod.h"
#include "demi/runtime/terrain/TerrainStreaming.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace demi::runtime;

namespace {
// A flat field is the controlled case for every rule here, exactly as it is for
// TerrainLod: its decimation error is zero, so a chunk's level is decided by the
// distance table alone and a hand computation can predict it. Every assertion
// below about WHICH level a chunk holds would be unfalsifiable on a noisy field.
TerrainRecipe flatRecipe(int cells, int chunkCells) {
  TerrainRecipe recipe;
  recipe.size = {float(cells), float(cells)};
  recipe.cellsX = recipe.cellsZ = cells;
  recipe.chunkCells = chunkCells;
  recipe.landforms.at("default").baseHeight = 8;
  recipe.landforms.at("default").heightVariation = 0;
  return recipe;
}

HeightField generate(const TerrainRecipe &recipe) {
  auto field = TerrainGenerator::generate(recipe);
  assert(field.has_value());
  return *field;
}

// The streaming module's own distance rule, restated here so the tests measure it
// independently rather than trusting the module to grade its own homework.
float nearestDistance(const HeightField &field, const TerrainChunk &chunk,
                      Vec2 camera) {
  const Vec2 first = field.position(chunk.firstCellX, chunk.firstCellZ);
  const Vec2 last =
      field.position(chunk.firstCellX + chunk.cellsX, chunk.firstCellZ + chunk.cellsZ);
  const float dx = std::max({0.F, first.x - camera.x, camera.x - last.x});
  const float dz = std::max({0.F, first.y - camera.y, camera.y - last.y});
  return std::hypot(dx, dz);
}

// The distance table on its own: how many thresholds this distance has passed.
int levelIndexFor(float distance) {
  int index = 0;
  for (const float threshold : {40.F, 110.F, 260.F, 600.F})
    if (distance >= threshold)
      ++index;
  return index;
}

TerrainStreamingState settle(const HeightField &field, Vec2 camera, float viewDistance,
                             const TerrainStreamingRelevance &relevance,
                             int steps = 30, float dt = 0.1F) {
  TerrainStreamingState state;
  for (int step = 0; step < steps; ++step)
    state = updateTerrainStreaming(state, field, camera, viewDistance, dt, relevance);
  return state;
}

bool sameState(const TerrainStreamingState &a, const TerrainStreamingState &b) {
  if (a.resident != b.resident || a.evicted != b.evicted || a.promoted != b.promoted ||
      a.demoted != b.demoted || a.chunks.size() != b.chunks.size())
    return false;
  if (a.triangles() != b.triangles())
    return false;
  for (std::size_t index = 0; index < a.chunks.size(); ++index) {
    const TerrainChunkResidency &x = a.chunks[index];
    const TerrainChunkResidency &y = b.chunks[index];
    if (x.chunkIndex != y.chunkIndex || x.level != y.level ||
        x.resident != y.resident || x.gameplayHeld != y.gameplayHeld ||
        x.outsideForSeconds != y.outsideForSeconds ||
        x.coarserForSeconds != y.coarserForSeconds || x.reason != y.reason ||
        x.firstCellX != y.firstCellX || x.firstCellZ != y.firstCellZ ||
        x.cellsX != y.cellsX || x.cellsZ != y.cellsZ)
      return false;
  }
  return true;
}

std::string refusalMessage(const std::function<void()> &action) {
  try {
    action();
  } catch (const std::invalid_argument &error) {
    return error.what();
  }
  return {};
}

bool mentions(const std::string &text, const char *needle) {
  return text.find(needle) != std::string::npos;
}

// Determinism first, and everything else below is worthless without it: a policy
// whose answer depends on anything but its inputs cannot be reasoned about, and
// the hysteresis is only meaningful if it is reproducible.
void identicalInputsGiveAnIdenticalState() {
  const auto field = generate(flatRecipe(64, 16));
  TerrainStreamingRelevance relevance;
  // A camera-only policy would leave gameplay holding untested, so this run has
  // an anchor in one corner and a camera that walks away from it.
  relevance.gameplayAnchors = {Vec3{4, 0, 4}};
  relevance.gameplayRadius = 24.F;

  // The same walk, twice, plus a separately generated but identical field: the
  // answer has to depend on the geometry and not on object identity.
  TerrainStreamingState first;
  TerrainStreamingState second;
  const auto twin = generate(flatRecipe(64, 16));
  TerrainStreamingState third;
  std::size_t promotions = 0, demotions = 0, evictedSeen = 0, heldSeen = 0;
  for (int step = 0; step < 40; ++step) {
    const Vec2 camera{4.F + float(step) * 1.4F, 4.F + float(step) * 1.1F};
    first = updateTerrainStreaming(first, field, camera, 45.F, 0.1F, relevance);
    second = updateTerrainStreaming(second, field, camera, 45.F, 0.1F, relevance);
    third = updateTerrainStreaming(third, twin, camera, 45.F, 0.1F, relevance);
    assert(sameState(first, second));
    assert(sameState(first, third));
    promotions += first.promoted;
    demotions += first.demoted;
    evictedSeen = std::max(evictedSeen, first.evicted);
    for (const TerrainChunkResidency &chunk : first.chunks)
      if (chunk.gameplayHeld)
        ++heldSeen;
  }
  // The comparison above is only worth anything if the state really moved.
  assert(promotions > 0);
  assert(demotions > 0);
  assert(evictedSeen > 0);
  assert(heldSeen > 0);
}

// A chunk's level is its distance's answer, so the order of the distances is the
// order of the levels. viewDistance is 200 on a 64 unit field, so nothing is
// evicted and eviction cannot be mistaken for a level change.
void nearerChunksHoldFinerLevels() {
  const auto field = generate(flatRecipe(64, 16));
  assert(field.chunks.size() == 16);
  TerrainStreamingRelevance relevance;
  const Vec2 camera{3, 3};
  const TerrainStreamingState state =
      settle(field, camera, 200.F, relevance, 1);

  float previousDistance = -1.F;
  int previousIndex = -1;
  int finest = terrainLodIndex(TerrainLod::Off);
  int coarsest = terrainLodIndex(TerrainLod::Full);
  // Chunk order is not distance order, so the pairs are sorted by distance before
  // the ordering is checked. Checking it in chunk order would only assert that the
  // generator emits chunks in some order.
  std::vector<std::pair<float, int>> byDistance;
  for (std::size_t index = 0; index < field.chunks.size(); ++index) {
    const float distance = nearestDistance(field, field.chunks[index], camera);
    const int level = terrainLodIndex(state.chunks[index].level);
    assert(state.chunks[index].resident);
    // The level matches the distance table on its own, which is what makes the
    // streaming budget predictable from the camera alone.
    assert(level == levelIndexFor(distance));
    byDistance.emplace_back(distance, level);
    finest = std::min(finest, level);
    coarsest = std::max(coarsest, level);
  }
  std::sort(byDistance.begin(), byDistance.end());
  for (const auto &[distance, level] : byDistance) {
    assert(distance >= previousDistance);
    assert(level >= previousIndex);
    previousDistance = distance;
    previousIndex = level;
  }
  // More than one level is present, so the ordering above is not a tautology.
  assert(finest == terrainLodIndex(TerrainLod::Full));
  assert(coarsest > terrainLodIndex(TerrainLod::Full));
  assert(coarsest == terrainLodIndex(TerrainLod::Half));
}

// `viewDistance` is the eviction radius and nothing else. Inside it a chunk lives,
// outside it a chunk dies, and the level inside is the LOD selector's business
// rather than a consequence of the radius.
void chunksBeyondTheViewDistanceAreEvicted() {
  const auto field = generate(flatRecipe(64, 16));
  TerrainStreamingRelevance relevance;
  const Vec2 camera{2, 2};
  const float viewDistance = 18.F;

  // The first update has served only a tenth of a second, so nothing may leave
  // yet. An eviction that ignored the delay would drop every far chunk on the
  // frame a distant camera appeared.
  const TerrainStreamingState first =
      updateTerrainStreaming({}, field, camera, viewDistance, 0.1F, relevance);
  assert(first.evicted == 0);
  assert(first.resident == field.chunks.size());

  const TerrainStreamingState state = settle(field, camera, viewDistance, relevance);
  assert(state.evicted > 0);
  assert(state.resident > 0);
  assert(state.resident + state.evicted == field.chunks.size());
  for (std::size_t index = 0; index < field.chunks.size(); ++index) {
    const bool wanted = nearestDistance(field, field.chunks[index], camera) <= viewDistance;
    assert(state.chunks[index].resident == wanted);
    if (!wanted)
      assert(state.chunks[index].level == TerrainLod::Off);
  }
  // Nothing held, nothing promoted, and the resident set shrank rather than
  // blurring: an eviction is not a demotion.
  for (const TerrainChunkResidency &chunk : state.chunks)
    assert(!chunk.gameplayHeld);
  assert(state.demoted == 0 && state.promoted == 0);
}

// THE INVERSION. The far corner is dropped when nothing wants it and kept when
// gameplay does, at the same camera and the same view distance. Camera-driven
// streaming alone would have thrown that chunk away, and the player standing on
// it would have fallen through the world.
void gameplayAnchorsOutrankTheCamera() {
  const auto field = generate(flatRecipe(64, 16));
  // The chunk in the opposite corner from the camera.
  const std::size_t far = 15;
  assert(field.chunks[far].firstCellX == 48 && field.chunks[far].firstCellZ == 48);

  TerrainStreamingRelevance relevance;
  // Placed OUTSIDE the chunk it keeps, so shrinking the radius can actually let
  // go of it. An anchor sitting inside the chunk's own footprint is at distance
  // zero from it and no radius below infinity would ever drop it.
  relevance.gameplayAnchors = {Vec3{70, 5, 70}};
  relevance.gameplayRadius = 12.F;
  const Vec2 camera{2, 2};
  const float viewDistance = 18.F;

  const TerrainStreamingState held = settle(field, camera, viewDistance, relevance);
  assert(held.chunks[far].resident);
  assert(held.chunks[far].gameplayHeld);

  // The SAME camera and the SAME view distance with the anchor removed: the chunk
  // the camera cannot see is gone, which is what makes the anchor the cause.
  TerrainStreamingRelevance cameraOnly;
  const TerrainStreamingState abandoned =
      settle(field, camera, viewDistance, cameraOnly);
  assert(!abandoned.chunks[far].resident);
  assert(!abandoned.chunks[far].gameplayHeld);

  // However far the camera moves, an anchored chunk stays resident and held.
  for (const Vec2 position : std::vector<Vec2>{{2, 2},   {60, 4},   {4, 60},
                                              {60, 60},  {30, 30}, {62, 30}}) {
    const TerrainStreamingState state =
        settle(field, position, viewDistance, relevance, 8);
    assert(state.chunks[far].resident);
    assert(state.chunks[far].gameplayHeld);
    assert(mentions(state.chunks[far].reason, "gameplay anchor"));
    // Held means held: never below the floor, and never evicted.
    assert(terrainLodIndex(state.chunks[far].level) <=
           terrainLodIndex(relevance.minimumResidentLod));
  }

  // The anchor decides residency, not detail: a chunk nobody can see is still
  // dropped once the anchor stops reaching it.
  TerrainStreamingRelevance shrunk = relevance;
  shrunk.gameplayRadius = 1.F;
  const TerrainStreamingState lost = settle(field, camera, viewDistance, shrunk);
  assert(!lost.chunks[far].resident);
}

// A resident chunk always has geometry. The floor is what guarantees that, and an
// `Off` floor is read as the coarsest level that still has some.
void minimumResidentLodIsAFloor() {
  const auto field = generate(flatRecipe(64, 16));
  const std::size_t far = 15;
  TerrainStreamingRelevance relevance;
  // Thresholds tightened so the camera's distance from the far chunk is already
  // past the whole table. On a 64 unit field the default table can never answer
  // `Off`, and a floor that never binds is not a floor.
  relevance.lod.distanceThresholds = {8.F, 16.F, 32.F, 64.F};
  relevance.gameplayAnchors = {Vec3{70, 0, 70}};
  relevance.gameplayRadius = 12.F;
  const Vec2 camera{2, 2};
  const float distance = nearestDistance(field, field.chunks[far], camera);
  assert(distance > 64.F);
  assert(terrainLodForChunk(field, field.chunks[far], distance, relevance.lod) ==
         TerrainLod::Off);

  for (const TerrainLod floor : {TerrainLod::Full, TerrainLod::Half,
                                 TerrainLod::Quarter, TerrainLod::Coarse}) {
    relevance.minimumResidentLod = floor;
    const TerrainStreamingState state = settle(field, camera, 18.F, relevance, 8);
    assert(state.chunks[far].resident);
    assert(state.chunks[far].gameplayHeld);
    assert(state.chunks[far].level == floor);
    assert(mentions(state.chunks[far].reason, "floored"));
  }
  // `Off` is the absence of a mesh rather than a level of one, so it clamps to the
  // coarsest level that still builds a mesh.
  relevance.minimumResidentLod = TerrainLod::Off;
  const TerrainStreamingState clamped = settle(field, camera, 18.F, relevance, 8);
  assert(clamped.chunks[far].resident);
  assert(clamped.chunks[far].level == TerrainLod::Coarse);
  assert(mentions(clamped.chunks[far].reason, "floored"));
}

// THE ASYMMETRY. Finer is immediate, coarser waits. This is the whole reason a
// camera can sit on a threshold without the ground flickering under it: the
// response a player would notice is the slow one.
void promotionIsImmediateAndDemotionWaits() {
  const auto field = generate(flatRecipe(64, 16));
  TerrainStreamingRelevance relevance;
  const std::size_t near = 0; // the chunk the camera starts on
  assert(field.chunks[near].firstCellX == 0 && field.chunks[near].firstCellZ == 0);

  TerrainStreamingState state = settle(field, Vec2{2, 2}, 200.F, relevance);
  assert(state.chunks[near].level == TerrainLod::Full);

  // The camera jumps to the opposite corner. Every chunk now wants to be coarser,
  // and NONE of them may move on the first update: the delay has not been served.
  state = updateTerrainStreaming(state, field, Vec2{60, 60}, 200.F, 0.1F, relevance);
  assert(state.demoted == 0);
  assert(state.chunks[near].level == TerrainLod::Full);
  assert(state.chunks[near].coarserForSeconds > 0.F);

  int updates = 0;
  while (state.demoted == 0 && updates < 40) {
    state = updateTerrainStreaming(state, field, Vec2{60, 60}, 200.F, 0.1F, relevance);
    ++updates;
  }
  // Nothing moved for most of a second, and then it moved on its own.
  assert(updates >= 5);
  assert(state.demoted > 0);
  assert(state.chunks[near].level == TerrainLod::Half);
  assert(state.chunks[near].coarserForSeconds == 0.F);

  // Coming back is immediate, on the very next update.
  const TerrainStreamingState returned =
      updateTerrainStreaming(state, field, Vec2{2, 2}, 200.F, 0.1F, relevance);
  assert(returned.promoted > 0);
  assert(returned.demoted == 0);
  assert(returned.chunks[near].level == TerrainLod::Full);
}

// THE THRASH TEST. A camera parked exactly on a level boundary, then one orbiting
// across it. Neither may make a chunk alternate between two levels.
void aCameraOnABoundaryDoesNotThrash() {
  const auto field = generate(flatRecipe(64, 16));
  TerrainStreamingRelevance relevance;
  const std::size_t watched = 0;
  // Chunk 0 spans [0,16] on both axes, so a camera at z = 56 is EXACTLY the 40
  // unit threshold away from it: 56 - 16 == 40, and the table counts a distance
  // that has reached a threshold as past it.
  const Vec2 boundary{8, 56};
  assert(nearestDistance(field, field.chunks[watched], boundary) == 40.F);
  assert(terrainLodIndex(terrainLodForChunk(field, field.chunks[watched], 40.F)) ==
         terrainLodIndex(TerrainLod::Half));

  // Parked exactly on the boundary, a chunk may settle once and then must never
  // move again. Fifty updates, no changes at all.
  const TerrainStreamingState parked =
      settle(field, boundary, 200.F, relevance, 30, 0.1F);
  TerrainStreamingState state = parked;
  for (int update = 0; update < 50; ++update) {
    const TerrainStreamingState next =
        updateTerrainStreaming(state, field, boundary, 200.F, 0.1F, relevance);
    assert(next.promoted == 0);
    assert(next.demoted == 0);
    // Everything, reasons included, is byte-identical: a static camera produces a
    // static state, so a caller can skip the work entirely.
    assert(sameState(state, next));
    state = next;
  }
  assert(state.chunks[watched].resident);

  // Orbiting ACROSS the boundary, on both sides of it, every single frame. The
  // wanted level alternates and the settled level must not: the coarser request
  // never survives long enough to be served, and the finer one is already there.
  TerrainStreamingState inside = settle(field, Vec2{8, 55.75F}, 200.F, relevance);
  assert(inside.chunks[watched].level == TerrainLod::Full);
  int changes = 0;
  int promotions = 0;
  int demotions = 0;
  TerrainLod held = inside.chunks[watched].level;
  for (int update = 0; update < 50; ++update) {
    // 39.75 units asks for Full, 40.25 asks for Half.
    const Vec2 camera{8, update % 2 == 0 ? 55.75F : 56.25F};
    const TerrainStreamingState next =
        updateTerrainStreaming(inside, field, camera, 200.F, 0.1F, relevance);
    if (next.chunks[watched].level != held) {
      ++changes;
      held = next.chunks[watched].level;
    }
    promotions += next.promoted;
    demotions += next.demoted;
    assert(next.chunks[watched].resident);
    inside = next;
  }
  // Never once: a full level is already the finest there is, so the alternating
  // request has nothing left to promote to.
  assert(changes == 0);
  assert(inside.chunks[watched].level == TerrainLod::Full);
  assert(promotions == 0 && demotions == 0);

  // The same orbit one level out, where the camera is asking for something
  // FINER every other frame. The finer answer is taken at once and the coarser one
  // is never served, so the orbit settles on the finer level instead of
  // alternating between the two.
  TerrainStreamingState outside = settle(field, Vec2{8, 71}, 200.F, relevance);
  assert(outside.chunks[watched].level == TerrainLod::Half);
  changes = 0;
  promotions = 0;
  demotions = 0;
  held = outside.chunks[watched].level;
  for (int update = 0; update < 50; ++update) {
    const Vec2 camera{8, update % 2 == 0 ? 55.75F : 71.F};
    const TerrainStreamingState next =
        updateTerrainStreaming(outside, field, camera, 200.F, 0.1F, relevance);
    if (next.chunks[watched].level != held) {
      ++changes;
      held = next.chunks[watched].level;
    }
    promotions += next.promoted;
    demotions += next.demoted;
    outside = next;
  }
  // One change, then fifty frames of nothing, and not one chunk anywhere in the
  // field got blurrier while the camera oscillated.
  assert(changes == 1);
  assert(outside.chunks[watched].level == TerrainLod::Full);
  assert(promotions > 0);
  assert(demotions == 0);
}

// A reason is a diagnostic, and a diagnostic that cannot be checked is a
// diagnostic nobody will read. Every chunk names its level, and the cause clause
// matches what actually happened to it.
void everyChunkExplainsItself() {
  const auto field = generate(flatRecipe(64, 16));
  TerrainStreamingRelevance relevance;
  relevance.gameplayAnchors = {Vec3{56, 0, 56}};
  relevance.gameplayRadius = 18.F;
  const TerrainStreamingState state = settle(field, Vec2{20, 20}, 18.F, relevance);
  assert(state.chunks.size() == field.chunks.size());

  bool sawHeld = false;
  bool sawEvicted = false;
  bool sawHeldByView = false;
  for (const TerrainChunkResidency &chunk : state.chunks) {
    assert(!chunk.reason.empty());
    // Every reason names the level it settled on, including an evicted chunk's.
    assert(mentions(chunk.reason, terrainLodName(chunk.level).data()));
    if (chunk.gameplayHeld) {
      sawHeld = true;
      assert(mentions(chunk.reason, "gameplay anchor"));
      assert(chunk.resident);
    } else if (!chunk.resident) {
      sawEvicted = true;
      assert(mentions(chunk.reason, "evicted"));
      assert(mentions(chunk.reason, terrainLodName(TerrainLod::Off).data()));
    } else {
      sawHeldByView = true;
      assert(mentions(chunk.reason, "camera"));
    }
  }
  assert(sawHeld && sawEvicted && sawHeldByView);

  // A chunk still inside its eviction delay says it is still resident rather than
  // claiming to have been thrown away, so an overlay cannot show a lie.
  const TerrainStreamingState cooling =
      updateTerrainStreaming({}, field, Vec2{2, 2}, 18.F, 0.1F, relevance);
  assert(cooling.evicted == 0);
  for (const TerrainChunkResidency &chunk : cooling.chunks) {
    assert(chunk.resident);
    assert(!chunk.reason.empty());
  }
}

// The budget a caller can take from a state alone: real chunk sizes, real levels,
// `terrainLodStep`, and nothing loaded.
void trianglesMatchTheChunkSizesAndLevels() {
  const auto field = generate(flatRecipe(512, 32));
  assert(field.chunks.size() == 256);
  assert(field.chunks.front().cellsX == 32 && field.chunks.front().cellsZ == 32);
  TerrainStreamingRelevance relevance;
  const Vec2 camera{256, 256};
  const float viewDistance = 200.F;
  const TerrainStreamingState state = settle(field, camera, viewDistance, relevance);

  // Hand-computed from the chunk rectangles, the distance table and the level
  // steps. Independent of the module under test, and exact because the field is
  // flat: a reduced mesh of flat ground deviates by nothing, so the geometric walk
  // never pulls a chunk finer than its distance asks for.
  std::size_t resident = 0;
  std::size_t expected = 0;
  for (const TerrainChunk &chunk : field.chunks) {
    const float distance = nearestDistance(field, chunk, camera);
    if (distance > viewDistance)
      continue;
    ++resident;
    const int index = levelIndexFor(distance);
    assert(index < terrainLodIndex(TerrainLod::Off));
    const TerrainLod level = TerrainLod(index);
    assert(state.chunks[chunk.firstCellZ / 32 * 16 + chunk.firstCellX / 32].level == level);
    const int step = terrainLodStep(level);
    assert(step > 0);
    // 32 cells divides by every stride, so no trailing far-edge sample.
    assert(chunk.cellsX % step == 0);
    const auto columns = std::size_t(chunk.cellsX / step + 1);
    const auto rows = std::size_t(chunk.cellsZ / step + 1);
    expected += (columns - 1) * (rows - 1) * 2;
  }
  assert(state.resident == resident);
  assert(state.triangles() == expected);
  // The measured figure: 148 resident chunks, 12 full, 40 half and 96 quarter,
  // which is 12 * 2048 + 40 * 512 + 96 * 128.
  assert(resident == 148);
  assert(state.triangles() == 57344);

  // And the meshes agree: what the budget predicts is what a renderer would build,
  // skirt-free, so the number is not a convenient approximation.
  std::size_t fromMeshes = 0;
  for (std::size_t index = 0; index < field.chunks.size(); ++index) {
    const TerrainChunkResidency &chunk = state.chunks[index];
    if (!chunk.resident)
      continue;
    const auto mesh = buildTerrainLodMesh(field, field.chunks[index], chunk.level, 0.F);
    assert(mesh.has_value());
    assert(mesh->indices.size() % 3 == 0);
    assert(mesh->skirtDepths.empty());
    fromMeshes += mesh->indices.size() / 3;
  }
  assert(fromMeshes == state.triangles());
}

// One entry per field chunk, always, in the field's own order, whatever the
// previous state claimed.
void theStateNeverLosesOrDuplicatesAChunk() {
  const auto field = generate(flatRecipe(64, 16));
  TerrainStreamingRelevance relevance;
  relevance.gameplayAnchors = {Vec3{60, 0, 60}};
  relevance.gameplayRadius = 20.F;

  TerrainStreamingState state;
  for (int step = 0; step < 30; ++step) {
    const Vec2 camera{4.F + float(step) * 2.F, 58.F - float(step) * 1.7F};
    state = updateTerrainStreaming(state, field, camera, 30.F, 0.1F, relevance);
    assert(state.chunks.size() == field.chunks.size());
    for (std::size_t index = 0; index < state.chunks.size(); ++index) {
      const TerrainChunkResidency &chunk = state.chunks[index];
      assert(chunk.chunkIndex == index);
      assert(chunk.firstCellX == field.chunks[index].firstCellX);
      assert(chunk.firstCellZ == field.chunks[index].firstCellZ);
      assert(chunk.cellsX == field.chunks[index].cellsX);
      assert(chunk.cellsZ == field.chunks[index].cellsZ);
    }
    assert(state.resident + state.evicted == state.chunks.size());
    // Level changes are counted between two resident states, so they can never
    // exceed the number of chunks that have one.
    assert(state.promoted + state.demoted <= state.resident);
  }

  // A previous state from a DIFFERENT chunking is ignored rather than applied to
  // the wrong rectangle, so streaming two terrains cannot bleed into each other.
  const auto coarse = generate(flatRecipe(64, 16));
  const auto fine = generate(flatRecipe(64, 8));
  assert(coarse.chunks.size() == 16 && fine.chunks.size() == 64);
  for (std::size_t index = 0; index < coarse.chunks.size(); ++index)
    assert(coarse.chunks[index].cellsX != fine.chunks[index].cellsX);
  const TerrainStreamingState stale =
      settle(coarse, Vec2{20, 20}, 30.F, relevance);
  const TerrainStreamingState fromStale =
      updateTerrainStreaming(stale, fine, Vec2{20, 20}, 30.F, 0.1F, relevance);
  const TerrainStreamingState fromNothing =
      updateTerrainStreaming({}, fine, Vec2{20, 20}, 30.F, 0.1F, relevance);
  assert(sameState(fromStale, fromNothing));

  // A field whose generator never chunked it is one chunk covering its own
  // extent, exactly as TerrainLod reads it, so the two never disagree about what
  // is being drawn.
  auto unchunked = field;
  unchunked.chunks.clear();
  const TerrainStreamingState whole =
      settle(unchunked, Vec2{2, 2}, 200.F, relevance, 4);
  assert(whole.chunks.size() == 1);
  assert(whole.chunks.front().cellsX == 64 && whole.chunks.front().cellsZ == 64);
  assert(whole.chunks.front().resident);
}

// Every refusal names its problem. A policy that silently clamped a negative time
// step or streamed a gap in the grid would produce plausible frames nobody could
// explain.
void badInputIsRefused() {
  const auto field = generate(flatRecipe(64, 16));
  TerrainStreamingRelevance relevance;
  const Vec2 camera{2, 2};

  assert(mentions(refusalMessage([&] {
                  (void)updateTerrainStreaming({}, field, camera, 30.F, -0.1F,
                                              relevance);
                }),
                  "negative"));
  assert(mentions(refusalMessage([&] {
                  (void)updateTerrainStreaming({}, field, camera, -30.F, 0.1F,
                                              relevance);
                }),
                  "view distance"));
  assert(mentions(refusalMessage([&] {
                  (void)updateTerrainStreaming({}, field, camera, 30.F,
                                              std::numeric_limits<float>::quiet_NaN(),
                                              relevance);
                }),
                  "time step"));
  assert(mentions(refusalMessage([&] {
                  TerrainStreamingRelevance broken = relevance;
                  broken.gameplayAnchors = {
                      Vec3{0, 0, std::numeric_limits<float>::infinity()}};
                  (void)updateTerrainStreaming({}, field, camera, 30.F, 0.1F, broken);
                }),
                  "anchor"));

  // A chunk list with a hole: total area no longer matches the grid.
  auto missing = field;
  missing.chunks.pop_back();
  assert(mentions(refusalMessage([&] {
                  (void)updateTerrainStreaming({}, missing, camera, 30.F, 0.1F,
                                              relevance);
                }),
                  "does not tile"));

  // Correct total area, an overlap and a gap: only the overlap test can catch it.
  auto sheared = field;
  sheared.chunks[1].cellsX += 4;
  sheared.chunks[2].cellsX -= 4;
  std::size_t area = 0;
  for (const TerrainChunk &chunk : sheared.chunks)
    area += std::size_t(chunk.cellsX) * std::size_t(chunk.cellsZ);
  assert(area == std::size_t(field.cellsX) * std::size_t(field.cellsZ));
  assert(mentions(refusalMessage([&] {
                  (void)updateTerrainStreaming({}, sheared, camera, 30.F, 0.1F,
                                              relevance);
                }),
                  "overlap"));

  // A chunk outside the grid cannot be streamed, from either entry point.
  TerrainChunk outside = field.chunks.front();
  outside.firstCellX = field.cellsX;
  assert(mentions(
      refusalMessage([&] {
        (void)terrainChunkTargetLevel(field, outside, camera, 30.F, relevance, 10.F);
      }),
      "does not fit"));

  // The single-chunk query agrees with the whole-state update, which is the whole
  // reason it exists: a tool can ask about one chunk without adopting a policy.
  TerrainStreamingState state;
  for (int step = 0; step < 20; ++step)
    state = updateTerrainStreaming(state, field, camera, 30.F, 0.1F, relevance);
  for (std::size_t index = 0; index < field.chunks.size(); ++index) {
    const TerrainChunkResidency &entry = state.chunks[index];
    const auto target = terrainChunkTargetLevel(
        field, field.chunks[index], camera, 30.F, relevance, entry.outsideForSeconds);
    if (target.has_value()) {
      // Either the level it holds, or a finer one it is still allowed to reach
      // after a demotion has been held back.
      assert(terrainLodIndex(entry.level) <= terrainLodIndex(*target));
    } else {
      assert(!entry.resident);
    }
  }
}
} // namespace

int main() {
  identicalInputsGiveAnIdenticalState();
  nearerChunksHoldFinerLevels();
  chunksBeyondTheViewDistanceAreEvicted();
  gameplayAnchorsOutrankTheCamera();
  minimumResidentLodIsAFloor();
  promotionIsImmediateAndDemotionWaits();
  aCameraOnABoundaryDoesNotThrash();
  everyChunkExplainsItself();
  trianglesMatchTheChunkSizesAndLevels();
  theStateNeverLosesOrDuplicatesAChunk();
  badInputIsRefused();
  std::cout << "Terrain streaming checks passed\n";
}
