#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainWorld.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {
using namespace demi::runtime;

template <class Samples>
constexpr bool readOnlySampleAccess =
    std::is_same_v<decltype(std::declval<Samples &>()[0]), const float &> &&
    std::is_same_v<decltype(std::declval<Samples &>().at(0)), const float &> &&
    std::is_same_v<decltype(std::declval<Samples &>().front()),
                   const float &> &&
    std::is_same_v<decltype(std::declval<Samples &>().back()), const float &> &&
    std::is_same_v<decltype(*std::declval<Samples &>().begin()),
                   const float &> &&
    std::is_same_v<decltype(std::declval<Samples &>().begin().operator->()),
                   const float *>;

static_assert(readOnlySampleAccess<TerrainSamples<float>>);
static_assert(readOnlySampleAccess<const TerrainSamples<float>>);
static_assert(!std::is_assignable_v<
              decltype(std::declval<TerrainSamples<float> &>()[0]), float>);
static_assert(std::is_nothrow_move_constructible_v<TerrainSamples<float>>);
static_assert(std::is_nothrow_move_assignable_v<TerrainSamples<float>>);
static_assert(
    std::is_same_v<decltype(std::declval<TerrainSamples<Vec3> &>()[0]),
                   const Vec3 &>);
static_assert(!std::is_assignable_v<
              decltype((std::declval<TerrainSamples<Vec3> &>()[0].x)), float>);

void check(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Operation>
void expectInvalid(Operation operation, const std::string &message) {
  try {
    operation();
  } catch (const std::invalid_argument &) {
    return;
  }
  throw std::runtime_error(message);
}

TerrainRecipe recipe(float height = 2) {
  TerrainRecipe result;
  result.size = {8, 8};
  result.cellsX = result.cellsZ = 8;
  result.chunkCells = 4;
  result.landforms.at("default").baseHeight = height;
  result.landforms.at("default").heightVariation = 0;
  return result;
}

std::shared_ptr<const HeightField> generate(const TerrainRecipe &source) {
  auto result = TerrainGenerator::generate(source);
  check(result.has_value(), "Fixture generation was cancelled");
  return std::make_shared<const HeightField>(std::move(*result));
}

TerrainUpdate regenerate(const TerrainRecipe &source,
                         std::shared_ptr<const HeightField> before) {
  auto result = regenerateTerrain(source, std::move(before), "History fixture");
  check(result && result->field && result->patch && result->patch->fullBefore &&
            result->patch->fullAfter,
        "Explicit regeneration requires both history snapshots");
  return std::move(*result);
}

TerrainUpdate update(const TerrainRecipe &before, const TerrainRecipe &after,
                     std::shared_ptr<const HeightField> field) {
  auto result = updateTerrain(before, after, std::move(field));
  check(result && result->field && result->patch, "Fixture update failed");
  return std::move(*result);
}

void sameField(const HeightField &actual, const HeightField &expected) {
  check(actual.size.x == expected.size.x && actual.size.y == expected.size.y &&
            actual.cellsX == expected.cellsX &&
            actual.cellsZ == expected.cellsZ &&
            actual.baseHeights == expected.baseHeights &&
            actual.heights == expected.heights &&
            actual.exclusions == expected.exclusions &&
            actual.biomeIndices == expected.biomeIndices &&
            actual.normals.size() == expected.normals.size() &&
            actual.biomeIds == expected.biomeIds &&
            actual.biomeColors.size() == expected.biomeColors.size() &&
            actual.chunks.size() == expected.chunks.size(),
        "Field values or layout differ");
  for (std::size_t index = 0; index < actual.normals.size(); ++index) {
    const auto left = actual.normals[index], right = expected.normals[index];
    check(left.x == right.x && left.y == right.y && left.z == right.z,
          "Field normals differ");
  }
  for (std::size_t index = 0; index < actual.biomeColors.size(); ++index) {
    const auto left = actual.biomeColors[index],
               right = expected.biomeColors[index];
    check(left.r == right.r && left.g == right.g && left.b == right.b &&
              left.a == right.a,
          "Field palettes differ");
  }
  for (std::size_t index = 0; index < actual.chunks.size(); ++index) {
    const auto left = actual.chunks[index], right = expected.chunks[index];
    check(left.firstCellX == right.firstCellX &&
              left.firstCellZ == right.firstCellZ &&
              left.cellsX == right.cellsX && left.cellsZ == right.cellsZ,
          "Field chunk layouts differ");
  }
}

void globalReplayRejectsStaleSource() {
  const auto initial = generate(recipe());
  const auto global = regenerate(recipe(3), initial);
  const auto stale = generate(recipe(8));
  expectInvalid([&] { (void)applyTerrainPatch(stale, *global.patch, true); },
                "Global redo accepted an unrelated source field");
  expectInvalid([&] { (void)applyTerrainPatch(stale, *global.patch, false); },
                "Global undo accepted an unrelated source field");
  sameField(*stale, *generate(recipe(8)));
}

void globalReplayAcceptsEquivalentSource() {
  const auto initial = generate(recipe());
  const auto global = regenerate(recipe(3), initial);
  // Local Undo/Redo creates equal snapshots with different object identities.
  const auto beforeCopy = std::make_shared<const HeightField>(*initial);
  const auto afterCopy = std::make_shared<const HeightField>(*global.field);
  sameField(*applyTerrainPatch(beforeCopy, *global.patch, true).field,
            *global.field);
  sameField(*applyTerrainPatch(afterCopy, *global.patch, false).field,
            *initial);
}

void globalMergeRejectsDisconnectedSnapshots() {
  const auto first = regenerate(recipe(3), generate(recipe()));
  const auto unrelated = regenerate(recipe(9), generate(recipe(8)));
  expectInvalid(
      [&] { (void)mergeTerrainPatches(*first.patch, *unrelated.patch); },
      "Merge joined unrelated global snapshot histories");
}

void globalMergeAcceptsEquivalentSnapshots() {
  const auto initial = generate(recipe());
  const auto first = regenerate(recipe(3), initial);
  const auto equivalent = std::make_shared<const HeightField>(*first.field);
  const auto next = regenerate(recipe(4), equivalent);
  const auto merged = mergeTerrainPatches(*first.patch, *next.patch);
  check(merged && merged->fullBefore && merged->fullAfter,
        "Global merge dropped its endpoint snapshots");
  sameField(*applyTerrainPatch(initial, *merged, true).field, *next.field);
  sameField(*applyTerrainPatch(next.field, *merged, false).field, *initial);
}

void paletteReplayRejectsStaleSource() {
  const auto before = recipe();
  auto after = before;
  after.biomes.at("default").color.r = .6F;
  const auto changed = update(before, after, generate(before));
  check(changed.patch->samples.empty() && changed.patch->beforePalette &&
            changed.patch->afterPalette && !changed.patch->fullBefore,
        "Tint fixture must use a palette-only local patch");
  auto unrelated = before;
  unrelated.biomes.at("default").color.r = .9F;
  const auto stale = generate(unrelated);
  expectInvalid([&] { (void)applyTerrainPatch(stale, *changed.patch, true); },
                "Palette redo accepted an unrelated source palette");
  expectInvalid([&] { (void)applyTerrainPatch(stale, *changed.patch, false); },
                "Palette undo accepted an unrelated source palette");
  sameField(*stale, *generate(unrelated));
}

void paletteMergeRejectsDisconnectedStates() {
  const auto before = recipe();
  auto firstRecipe = before;
  firstRecipe.biomes.at("default").color.r = .6F;
  const auto first = update(before, firstRecipe, generate(before));
  auto unrelatedBefore = before;
  unrelatedBefore.biomes.at("default").color.r = .8F;
  auto unrelatedAfter = unrelatedBefore;
  unrelatedAfter.biomes.at("default").color.r = .9F;
  const auto next =
      update(unrelatedBefore, unrelatedAfter, generate(unrelatedBefore));
  expectInvalid([&] { (void)mergeTerrainPatches(*first.patch, *next.patch); },
                "Merge joined noncontiguous palette-only histories");
}

void paletteMergeRoundTrip() {
  const auto before = recipe();
  auto firstRecipe = before;
  firstRecipe.biomes.at("default").color.r = .6F;
  auto finalRecipe = firstRecipe;
  finalRecipe.biomes.at("default").color.r = .9F;
  const auto initial = generate(before);
  const auto first = update(before, firstRecipe, initial);
  const auto next = update(firstRecipe, finalRecipe, first.field);
  const auto merged = mergeTerrainPatches(*first.patch, *next.patch);
  check(merged && merged->samples.empty() && !merged->fullBefore &&
            !merged->fullAfter,
        "Palette-only coalescing retained sample or whole-field history");
  sameField(*applyTerrainPatch(initial, *merged, true).field, *next.field);
  sameField(*applyTerrainPatch(next.field, *merged, false).field, *initial);
}

void failedLocalReplayPreservesSnapshot() {
  const auto before = recipe();
  auto after = before;
  after.edits.push_back({.kind = TerrainEditKind::Raise,
                         .center = {4, 4},
                         .radius = 2,
                         .amount = 1});
  const auto initial = generate(before);
  const auto changed = update(before, after, initial);
  check(changed.patch->samples.size() > 1,
        "Brush needs multiple changed samples");
  auto invalid = *changed.patch;
  invalid.samples.back().before.height += 1;
  const auto page = initial->heights.pageIdentity(0);
  expectInvalid([&] { (void)applyTerrainPatch(initial, invalid, true); },
                "Local replay accepted a stale sample");
  check(initial->heights.pageIdentity(0) == page,
        "Rejected replay detached the input snapshot");
  sameField(*initial, *generate(before));
}

void copyOnWritePageIsolation() {
  TerrainSamples<float> editable;
  editable.assign(3000, 2);
  const TerrainSamples<float> snapshot = editable;
  editable.set(1025, 7);
  check(snapshot[1025] == 2 && editable[1025] == 7,
        "Editing one copy changed another copy's sample");
  check(editable.pageIdentity(0) == snapshot.pageIdentity(0) &&
            editable.pageIdentity(1025) != snapshot.pageIdentity(1025) &&
            editable.pageIdentity(2048) == snapshot.pageIdentity(2048),
        "Copy-on-write detached untouched pages");
  editable.resize(1030);
  editable.resize(2050);
  const auto &read = std::as_const(editable);
  check(read[1025] == 7 && read[1030] == 0 && read[2048] == 0,
        "Shrink/regrow restored deleted samples instead of defaults");
  check(snapshot.size() == 3000 && snapshot[1030] == 2 && snapshot[2048] == 2,
        "Resizing a copy changed a retained snapshot");
}

void readOnlyReferenceAndSetPreserveSnapshot() {
  HeightField editable = *generate(recipe());
  const auto &read = editable.heights[0];
  const auto snapshot = std::make_shared<const HeightField>(editable);
  const float before = snapshot->heights[0];
  const auto sharedPage = snapshot->heights.pageIdentity(0);
  for (const float &height : editable.heights)
    check(height == before, "Read-only iteration changed sample values");
  check(editable.heights.at(0) == before &&
            editable.heights.front() == before &&
            editable.heights.back() == before &&
            editable.heights.pageIdentity(0) == sharedPage,
        "Reading samples detached shared storage");
  editable.heights.set(0, before + 1);
  check(snapshot->heights[0] == before && read == before &&
            editable.heights[0] == before + 1 &&
            editable.heights.pageIdentity(0) != sharedPage,
        "Explicit set mutated a retained snapshot or its read-only reference");
  try {
    editable.heights.set(editable.heights.size(), before);
    throw std::runtime_error("Out-of-range sample write was accepted");
  } catch (const std::out_of_range &) {
  }
  check(snapshot->heights[0] == before && editable.heights[0] == before + 1,
        "Rejected sample write changed snapshot values");
}

void movedSampleBufferRemainsValid() {
  TerrainSamples<float> source{2, 3};
  const TerrainSamples<float> moved = std::move(source);
  check(moved.size() == 2 && moved[0] == 2 && moved[1] == 3,
        "Moving samples lost their values");
  check(source.empty() && source.size() == 0 && source.pageCount() == 0 &&
            source.begin() == source.end(),
        "Move construction did not reset the source buffer");
  source.push_back(7);
  check(source.size() == 1 && std::as_const(source)[0] == 7,
        "Moved-from samples cannot be reused");
  TerrainSamples<float> assigned{8, 9, 10};
  assigned = std::move(source);
  check(assigned.size() == 1 && assigned[0] == 7 && source.empty() &&
            source.pageCount() == 0,
        "Move assignment did not transfer storage and reset the source");
  source.push_back(11);
  check(source.size() == 1 && source[0] == 11 && assigned[0] == 7,
        "Reusing a move-assigned buffer changed its destination");
  auto &self = assigned;
  assigned = std::move(self);
  check(assigned.size() == 1 && assigned[0] == 7,
        "Self-move invalidated sample storage");
}

World makeWorld(const TerrainRecipe &source) {
  World world;
  Entity owner;
  owner.id = "terrain";
  owner.setComponent(Transform3DComponent{});
  Terrain3DComponent terrain;
  terrain.recipe = source.toJson();
  owner.setComponent(std::move(terrain));
  world.entities.push_back(std::move(owner));
  std::string error;
  check(materializeTerrains(world, error), error);
  return world;
}

std::shared_ptr<const HeightField> current(const World &world) {
  const auto found = std::ranges::find(world.entities, "terrain", &Entity::id);
  check(found != world.entities.end(), "Fixture lost the terrain owner");
  return found->component<Terrain3DComponent>()->generated;
}

void publish(World &world, const TerrainRecipe &source,
             const TerrainUpdate &changed) {
  std::string error;
  check(updateTerrainWorld(world, "terrain", source.toJson(), changed, error),
        error);
}

TerrainRecipe sculpted(TerrainRecipe before) {
  before.edits.push_back({.kind = TerrainEditKind::Raise,
                          .center = {4, 4},
                          .radius = 2,
                          .amount = 1});
  return before;
}

void globalUndoAfterLocalUndo() {
  const auto before = recipe();
  const auto generatedRecipe = recipe(3);
  const auto brushedRecipe = sculpted(generatedRecipe);
  auto world = makeWorld(before);
  const auto initial = current(world);
  const auto global = regenerate(generatedRecipe, initial);
  publish(world, generatedRecipe, global);
  const auto brush = update(generatedRecipe, brushedRecipe, current(world));
  publish(world, brushedRecipe, brush);
  const auto undoBrush = applyTerrainPatch(current(world), *brush.patch, false);
  publish(world, generatedRecipe, undoBrush);
  sameField(*current(world), *global.field);
  const auto undoGlobal =
      applyTerrainPatch(current(world), *global.patch, false);
  // Equal reconstructed content must be accepted even if its pointer changed.
  publish(world, before, undoGlobal);
  sameField(*current(world), *initial);
}

void globalRedoAfterLocalRedo() {
  const auto before = recipe();
  const auto brushedRecipe = sculpted(before);
  const auto generatedRecipe = recipe(3);
  auto world = makeWorld(before);
  const auto initial = current(world);
  const auto brush = update(before, brushedRecipe, initial);
  publish(world, brushedRecipe, brush);
  const auto global = regenerate(generatedRecipe, current(world));
  publish(world, generatedRecipe, global);
  publish(world, brushedRecipe,
          applyTerrainPatch(current(world), *global.patch, false));
  publish(world, before,
          applyTerrainPatch(current(world), *brush.patch, false));
  publish(world, brushedRecipe,
          applyTerrainPatch(current(world), *brush.patch, true));
  sameField(*current(world), *global.patch->fullBefore);
  publish(world, generatedRecipe,
          applyTerrainPatch(current(world), *global.patch, true));
  sameField(*current(world), *global.field);
}
} // namespace

int main() {
  struct Case {
    const char *name;
    void (*run)();
  };
  const std::array cases{
      Case{"global replay rejects stale source",
           globalReplayRejectsStaleSource},
      Case{"global replay accepts equivalent source",
           globalReplayAcceptsEquivalentSource},
      Case{"global merge rejects disconnected snapshots",
           globalMergeRejectsDisconnectedSnapshots},
      Case{"global merge accepts equivalent snapshots",
           globalMergeAcceptsEquivalentSnapshots},
      Case{"palette replay rejects stale source",
           paletteReplayRejectsStaleSource},
      Case{"palette merge rejects disconnected states",
           paletteMergeRejectsDisconnectedStates},
      Case{"palette merge round trip", paletteMergeRoundTrip},
      Case{"failed local replay preserves snapshot",
           failedLocalReplayPreservesSnapshot},
      Case{"copy-on-write page isolation", copyOnWritePageIsolation},
      Case{"read-only reference and set preserve snapshot",
           readOnlyReferenceAndSetPreserveSnapshot},
      Case{"moved sample buffer remains valid", movedSampleBufferRemainsValid},
      Case{"global undo after local undo", globalUndoAfterLocalUndo},
      Case{"global redo after local redo", globalRedoAfterLocalRedo}};
  std::size_t failures = 0;
  for (const auto &test : cases) {
    try {
      test.run();
      std::cout << "PASS: " << test.name << '\n';
    } catch (const std::exception &exception) {
      ++failures;
      std::cerr << "FAIL: " << test.name << ": " << exception.what() << '\n';
    }
  }
  std::cout << "Terrain patch tests: " << cases.size() - failures << " passed, "
            << failures << " failed\n";
  return failures == 0 ? 0 : 1;
}
