#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainPreset.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "editor/EditorTerrainAuthoring.h"
#include "editor/EditorTerrainPicking.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
using namespace demi;

class TestSurface final : public editor::EditorTerrainSurface {
public:
  std::optional<runtime::Vec3> raycast(runtime::Vec3,
                                       runtime::Vec3) const override {
    return runtime::Vec3{8, 12, 8};
  }
  std::optional<float> height(runtime::Vec2) const override { return 12; }
  nlohmann::json protection(runtime::Vec2 center, float radius, float strength,
                            float falloff) const override {
    runtime::TerrainRecipe recipe;
    runtime::TerrainEdit edit;
    edit.kind = runtime::TerrainEditKind::Protect;
    edit.center = center;
    edit.radius = radius;
    edit.strength = strength;
    edit.falloff = falloff;
    edit.snapshotSize = recipe.size;
    edit.snapshotCellsX = recipe.cellsX;
    edit.snapshotCellsZ = recipe.cellsZ;
    recipe.edits.push_back(edit);
    return recipe.toJson()["edits"].back();
  }
};

std::optional<editor::EditorTerrainCommit>
nextBatch(editor::EditorTerrainAuthoring &authoring, std::string &error) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (authoring.busy() && std::chrono::steady_clock::now() < deadline) {
    auto commit = authoring.poll(error);
    if (commit)
      return commit;
    std::this_thread::yield();
  }
  assert(!authoring.busy());
  return std::nullopt;
}

std::optional<editor::EditorTerrainCommit>
finish(editor::EditorTerrainAuthoring &authoring, std::string &error) {
  while (auto batch = nextBatch(authoring, error)) {
    if (batch->final)
      return batch;
  }
  return std::nullopt;
}

// The engine's own preset, used as the applied landscape in editor tests. Its
// default grid matches TerrainRecipe::defaults() so "same grid" is a preset
// that changes generation settings without moving the terrain lattice.
runtime::TerrainPreset preset(std::string id = "asset://terrain/presets/alpine",
                              int seed = 4242, runtime::Vec2 size = {128, 128},
                              int cells = 128) {
  runtime::TerrainPreset value;
  value.id = std::move(id);
  value.name = "Alpine";
  value.label = "Alpine ridge";
  value.description = "Rock above the treeline, scree below it.";
  value.size = size;
  value.cellsX = value.cellsZ = cells;
  value.chunkCells = cells / 4;
  value.seed = seed;
  value.defaultBiome = "rock";
  value.biomes.clear();
  value.landforms.clear();
  value.defaultLandform = "rock";
  value.landforms.emplace("rock", runtime::TerrainLandform{.baseHeight = 4,
                                                           .heightVariation = 1,
                                                           .featureSize = 4});
  value.biomes.emplace(
      "rock",
      runtime::TerrainBiome{.landform = "rock", .color = {.5F, .5F, .5F, 1}});
  value.landforms.emplace(
      "scree", runtime::TerrainLandform{
                   .baseHeight = 4, .heightVariation = 1, .featureSize = 4});
  value.biomes.emplace(
      "scree",
      runtime::TerrainBiome{.landform = "scree", .color = {.6F, .6F, .6F, 1}});
  return value;
}
} // namespace

int main() {
  using namespace demi;
  const auto surface = std::make_shared<TestSurface>();
  const auto recipe = runtime::TerrainRecipe::defaults();
  {
    editor::EditorTerrainAuthoring sharedDraft;
    sharedDraft.bind("terrain-source", "first-placement", recipe, surface,
                     "asset://terrain/shared");
    sharedDraft.draft()["seed"] = 4242;
    sharedDraft.bind("terrain-source", "second-placement", recipe, surface,
                     "asset://terrain/shared");
    assert(sharedDraft.draft()["seed"] == 4242);
    assert(sharedDraft.entityId() == "second-placement");
    sharedDraft.discardDraft();
    sharedDraft.bind("terrain-source", "first-placement", recipe, surface,
                     "asset://terrain/shared");
    assert(!sharedDraft.hasDraftChanges());
    sharedDraft.unbind();
    sharedDraft.bind("terrain-source", "isolated-preview", recipe, surface,
                     "asset://terrain/shared");
    assert(!sharedDraft.hasDraftChanges());
  }
  editor::EditorTerrainAuthoring authoring(
      [surface](const auto &, std::stop_token,
                const std::function<void(float)> &progress, std::string &) {
        progress(1);
        return surface;
      },
      [surface](const auto &, const auto &, auto, std::stop_token,
                const std::function<void(float)> &, std::string &) {
        return std::optional<editor::EditorTerrainUpdate>{{surface, {}}};
      });
  std::string error;
  // Resolution happens before the worker starts; only the immutable palette
  // snapshot crosses the authoring boundary.
  runtime::TerrainRecipe paletteRecipe;
  paletteRecipe.size = {8, 8};
  paletteRecipe.cellsX = paletteRecipe.cellsZ = 8;
  paletteRecipe.paletteId = "asset://terrain/palettes/editor_test";
  auto palette = std::make_shared<runtime::TerrainPalette>();
  palette->id = paletteRecipe.paletteId;
  palette->placements.emplace(
      "grass",
      runtime::TerrainPaletteEntry{.model = "asset://terrain/props/grass"});
  int resolutions = 0;
  editor::EditorTerrainAuthoring resolved;
  resolved.setInputResolver([&](const runtime::TerrainRecipe &requested) {
    assert(requested.paletteId == paletteRecipe.paletteId);
    ++resolutions;
    return runtime::TerrainGenerationInputs{palette, "editor-snapshot"};
  });
  resolved.bind("palette-scene", "terrain", paletteRecipe.toJson(), {});
  assert(resolved.generate(std::nullopt, error));
  assert(resolutions == 1);
  const auto paletteCommit = finish(resolved, error);
  assert(paletteCommit && paletteCommit->surface->heightField());
  assert(paletteCommit->surface->heightField()->inputFingerprint ==
         "editor-snapshot");
  assert(paletteCommit->surface->heightField()->resolvedPalette);
  editor::EditorTerrainAuthoring missingResolver;
  missingResolver.bind("palette-scene", "terrain", paletteRecipe.toJson(), {});
  error.clear();
  assert(!missingResolver.generate(std::nullopt, error));
  assert(error.find("requires an asset resolver") != std::string::npos);
  error.clear();
  editor::EditorTerrainAuthoring changedPalette;
  changedPalette.setInputResolver([](const runtime::TerrainRecipe &requested) {
    auto selected = std::make_shared<runtime::TerrainPalette>();
    selected->id = requested.paletteId;
    return runtime::TerrainGenerationInputs{selected, "changed-snapshot"};
  });
  changedPalette.bind("palette-scene", "terrain", paletteRecipe.toJson(),
                      paletteCommit->surface);
  changedPalette.draft()["palette"] = "asset://terrain/palettes/another";
  assert(!changedPalette.generate(std::nullopt, error));
  assert(error.find("update path needs resolved") != std::string::npos);
  error.clear();
  changedPalette.setResolvedUpdater(editor::updateEditorTerrainWithInputs);
  assert(changedPalette.generate(std::nullopt, error));
  const auto changedCommit = finish(changedPalette, error);
  assert(changedCommit && changedCommit->patch &&
         changedCommit->patch->invalidation.fullGeneration);
  assert(changedCommit->surface->heightField()->paletteId ==
         "asset://terrain/palettes/another");
  assert(changedCommit->surface->heightField()->inputFingerprint ==
         "changed-snapshot");
  editor::EditorTerrainAuthoring removedPalette;
  removedPalette.setInputResolver([](const runtime::TerrainRecipe &) {
    return runtime::TerrainGenerationInputs{};
  });
  removedPalette.setResolvedUpdater(editor::updateEditorTerrainWithInputs);
  removedPalette.bind("palette-scene", "terrain", changedCommit->recipe,
                      changedCommit->surface);
  removedPalette.draft().erase("palette");
  assert(removedPalette.generate(std::nullopt, error));
  const auto removedCommit = finish(removedPalette, error);
  assert(removedCommit && removedCommit->patch &&
         removedCommit->patch->invalidation.fullGeneration);
  const auto clearedField = removedCommit->surface->heightField();
  assert(clearedField && clearedField->paletteId.empty() &&
         clearedField->inputFingerprint.empty() &&
         !clearedField->resolvedPalette &&
         clearedField->scatterPlacements.empty());

  editor::EditorTerrainAuthoring drafts;
  drafts.bind("first-scene", "terrain", recipe, surface);
  drafts.draft()["seed"] = 101;
  drafts.bind("first-scene", "other-terrain", recipe, surface);
  drafts.draft()["seed"] = 202;
  drafts.bind("second-scene", "terrain", recipe, surface);
  drafts.draft()["seed"] = 303;
  drafts.bind("first-scene", "terrain", recipe, surface);
  assert(drafts.draft()["seed"] == 101);
  drafts.bind("first-scene", "other-terrain", recipe, surface);
  assert(drafts.draft()["seed"] == 202);
  drafts.unbind();
  drafts.bind("second-scene", "terrain", recipe, surface);
  assert(drafts.draft()["seed"] == 303);
  drafts.bind("first-scene", "terrain", recipe, surface);
  auto changedBaseline = recipe;
  changedBaseline["seed"] = 404;
  drafts.bind("first-scene", "other-terrain", recipe, surface);
  drafts.bind("first-scene", "terrain", changedBaseline, surface);
  assert(drafts.draft() == changedBaseline);
  assert(!drafts.takeNotice().empty());
  assert(drafts.takeNotice().empty());

  // Discard preserves the exact authored shape and removes the saved draft.
  const nlohmann::json sparseRecipe = {{"format_version", 1}};
  drafts.bind("sparse-scene", "terrain", sparseRecipe, surface);
  drafts.draft()["biomes"] = recipe["biomes"];
  drafts.draft()["biomes"]["draft-only"] = recipe["biomes"]["default"];
  drafts.brush.biome = "draft-only";
  drafts.discardDraft();
  assert(drafts.draft() == sparseRecipe);
  assert(drafts.brush.biome == "default");
  drafts.bind("first-scene", "terrain", changedBaseline, surface);
  drafts.bind("sparse-scene", "terrain", sparseRecipe, surface);
  assert(drafts.draft() == sparseRecipe);
  auto lakeRecipe = recipe;
  lakeRecipe["biomes"]["lake"] = recipe["biomes"]["default"];
  lakeRecipe["default_biome"] = "lake";
  drafts.brush.biome = "unknown";
  drafts.bind("lake-scene", "terrain", lakeRecipe, surface);
  assert(drafts.brush.biome == "lake");

  authoring.bind("scene", "terrain", recipe, surface);
  authoring.draft()["seed"] = 42;
  authoring.bind("scene", "terrain", recipe, surface);
  assert(authoring.draft()["seed"] ==
         42); // Selection polling preserves drafts.
  assert(authoring.generate(std::nullopt, error));
  assert(authoring.surface() == surface); // Previous preview remains visible.
  const auto generated = finish(authoring, error);
  assert(generated && generated->before == recipe &&
         generated->recipe["seed"] == 42);
  assert(generated->document == "scene" && generated->entityId == "terrain");

  authoring.bind("scene", "terrain", generated->recipe, surface);
  assert(authoring.takeNotice().empty());
  authoring.brush.mode = editor::EditorTerrainBrush::Flatten;
  assert(authoring.update(
      {.hovered = true, .focused = true, .leftPressed = true, .leftDown = true},
      runtime::Vec3{8, 12, 8}, error));
  assert(authoring.update({.hovered = true, .focused = true, .leftDown = true},
                          runtime::Vec3{12, 30, 8}, error));
  assert(authoring.update({.focused = true, .leftReleased = true}, {}, error));
  const auto stroke = finish(authoring, error);
  assert(stroke && stroke->recipe["edits"].size() == 1);
  assert(stroke->recipe["edits"][0]["points"].size() >= 2);
  for (const auto &edit : stroke->recipe["edits"])
    assert(edit["target_height"] == 12); // Flatten holds initial hit height.
  assert(!authoring.poll(error));        // One completion per entire stroke.
  authoring.bind("scene", "terrain", stroke->recipe, surface);
  assert(authoring.brush.mode == editor::EditorTerrainBrush::Flatten);

  const auto beforeCancel = authoring.draft();
  assert(authoring.update(
      {.hovered = true, .focused = true, .leftPressed = true, .leftDown = true},
      runtime::Vec3{8, 12, 8}, error));
  assert(authoring.update({.focused = true, .cancelPressed = true}, {}, error));
  assert(!finish(authoring, error)); // Drain the cancelled incremental worker.
  assert(!authoring.stroking() && !authoring.busy());
  assert(authoring.draft() == beforeCancel);

  authoring.draft()["resolution"] = {32, 32};
  assert(authoring.needsResizeDecision());
  assert(!authoring.generate(std::nullopt, error));
  error.clear();
  assert(authoring.generate(editor::EditorTerrainResize::Clear, error));
  const auto resized = finish(authoring, error);
  assert(resized && resized->recipe["edits"].empty());
  assert(resized->before == stroke->recipe);

  // Missing surface hits never fabricate a ground-plane sculpt operation.
  authoring.bind("other-scene", "terrain", recipe, surface);
  authoring.brush.mode = editor::EditorTerrainBrush::Raise;
  assert(authoring.update(
      {.hovered = true, .focused = true, .leftPressed = true, .leftDown = true},
      {}, error));
  assert(!authoring.stroking());

  // A stopped worker may still return a valid field; cancellation rejects it.
  editor::EditorTerrainAuthoring cancellable(
      [surface](const auto &, std::stop_token stop,
                const std::function<void(float)> &, std::string &) {
        std::mutex mutex;
        std::condition_variable_any condition;
        std::unique_lock lock(mutex);
        condition.wait(lock, stop, [] { return false; });
        return surface;
      });
  cancellable.bind("scene", "terrain", recipe, surface);
  cancellable.draft()["seed"] = 999;
  assert(cancellable.generate(std::nullopt, error));
  cancellable.cancel();
  assert(!finish(cancellable, error));
  assert(cancellable.surface() == surface);
  assert(cancellable.draft()["seed"] == 999);
  assert(cancellable.generate(std::nullopt, error));
  cancellable.bind("new-scene", "terrain", recipe, surface);
  assert(!finish(cancellable, error)); // Old document result cannot land here.
  cancellable.bind("scene", "terrain", recipe, surface);
  assert(cancellable.draft()["seed"] == 999);

  editor::EditorTerrainAuthoring failing(
      [](const auto &, std::stop_token, const std::function<void(float)> &,
         std::string &) -> editor::EditorTerrainSurfacePtr {
        throw std::runtime_error("generator error");
      });
  failing.bind("scene", "terrain", recipe, surface);
  assert(failing.generate(std::nullopt, error));
  assert(!finish(failing, error));
  assert(error == "generator error" && failing.surface() == surface);
  error.clear();

  // Protection grid changes require an explicit loss decision.
  auto protectedRecipe = recipe;
  protectedRecipe["edits"].push_back(surface->protection({8, 8}, 2, 1, 0));
  authoring.bind("protected-scene", "terrain", protectedRecipe, surface);
  authoring.draft()["size"] = {64, 64};
  assert(authoring.needsResizeDecision());
  assert(!authoring.generate(editor::EditorTerrainResize::Keep, error));
  error.clear();
  assert(authoring.generate(editor::EditorTerrainResize::Clear, error));
  const auto cleared = finish(authoring, error);
  assert(cleared && cleared->recipe["edits"].empty());
  assert(cleared->before == protectedRecipe);

  // Picking uses the actual elevated surface through parent translation,
  // rotation, and nonuniform scale, rather than an authoring ground plane.
  runtime::TerrainRecipe elevatedRecipe;
  elevatedRecipe.size = {16, 16};
  elevatedRecipe.cellsX = 4;
  elevatedRecipe.cellsZ = 4;
  elevatedRecipe.landforms["default"].baseHeight = 12;
  elevatedRecipe.landforms["default"].heightVariation = 0;
  const auto elevated =
      editor::generateEditorTerrain(elevatedRecipe.toJson(), {}, {}, error);
  assert(elevated && error.empty());
  runtime::World world;
  runtime::Entity parent;
  parent.id = "parent";
  parent.setComponent(runtime::Transform3DComponent{
      .position = {20, 5, -10}, .rotation = {0, 0.4F, 0}, .scale = {2, 3, 4}});
  world.entities.push_back(std::move(parent));
  runtime::Entity terrain;
  terrain.id = "terrain";
  terrain.setComponent(runtime::Transform3DComponent{.parent = "parent"});
  world.entities.push_back(std::move(terrain));
  const auto transform =
      runtime::resolveWorldTransform3D(world, world.entities.back());
  assert(transform);
  editor::EditorSceneViewCamera camera;
  camera.position = runtime::transformPoint3D(*transform, {8, 40, 8});
  camera.forward = runtime::transformDirection3D(*transform, {0, -1, 0});
  camera.up = runtime::transformDirection3D(*transform, {0, 0, 1});
  for (const bool perspective : {true, false}) {
    camera.projection.perspective = perspective;
    const auto hit = editor::pickEditorTerrain(
        world, "terrain", elevated, camera,
        {.mousePosition = {400, 300}, .viewportSize = {800, 600}});
    assert(hit && std::abs(hit->x - 8) < 0.001F &&
           std::abs(hit->y - 12) < 0.001F && std::abs(hit->z - 8) < 0.001F);
  }
  authoring.bind("elevated", "terrain", elevatedRecipe.toJson(), elevated);
  authoring.brush.mode = editor::EditorTerrainBrush::Protect;
  authoring.brush.radius = 2;
  assert(authoring.update(
      {.hovered = true, .focused = true, .leftPressed = true, .leftDown = true},
      runtime::Vec3{8, 12, 8}, error));
  assert(authoring.update({.focused = true, .leftReleased = true}, {}, error));
  const auto protectionStroke = finish(authoring, error);
  assert(protectionStroke);
  const auto protectionRecipe =
      runtime::TerrainRecipe::parse(protectionStroke->recipe);
  assert(protectionRecipe.edits.back().snapshotCellsX == 4);
  assert(protectionRecipe.edits.back().strength == 1);
  assert(protectionRecipe.edits.back().falloff == 0);
  assert(!protectionRecipe.edits.back().samples.empty());

  // Incremental brushes consume the retained native field, publish while the
  // pointer is down, and retain sparse history across multiple live batches.
  runtime::TerrainRecipe liveRecipe;
  liveRecipe.size = {32, 24};
  liveRecipe.cellsX = 32;
  liveRecipe.cellsZ = 24;
  liveRecipe.chunkCells = 4;
  liveRecipe.landforms.at("default").heightVariation = 0;
  const auto liveJson = liveRecipe.toJson();
  const auto liveSurface =
      editor::generateEditorTerrain(liveJson, {}, {}, error);
  int fullGenerations = 0;
  std::atomic<int> batches{0};
  std::promise<void> firstStarted, resumeFirst;
  const auto resume = resumeFirst.get_future().share();
  editor::EditorTerrainAuthoring live(
      [&](const auto &, std::stop_token, const auto &,
          std::string &) -> editor::EditorTerrainSurfacePtr {
        ++fullGenerations;
        throw std::runtime_error("Brush called the full generator");
      },
      [&](const auto &before, const auto &after, auto previous,
          std::stop_token stop, const auto &progress, std::string &issue) {
        if (++batches == 1) {
          firstStarted.set_value();
          resume.wait();
        }
        return editor::updateEditorTerrain(before, after, previous, stop,
                                           progress, issue);
      });
  live.bind("live", "terrain", liveJson, liveSurface);
  live.brush.mode = editor::EditorTerrainBrush::Raise;
  live.brush.radius = 1;
  live.brush.falloff = 0;
  const editor::EditorViewportToolInput down{
      .hovered = true, .focused = true, .leftDown = true};
  assert(live.update(
      {.hovered = true, .focused = true, .leftPressed = true, .leftDown = true},
      runtime::Vec3{2, 0, 2}, error));
  assert(firstStarted.get_future().wait_for(std::chrono::seconds(3)) ==
         std::future_status::ready);
  assert(live.update(down, runtime::Vec3{3, 0, 2}, error));
  assert(live.update(down, runtime::Vec3{4, 0, 2}, error));
  assert(batches == 1); // Latest stamps wait behind one in-flight batch.
  resumeFirst.set_value();
  const auto firstPreview = nextBatch(live, error);
  assert(firstPreview && !firstPreview->final && live.stroking());
  assert(firstPreview->surface->height({2, 2}) > liveSurface->height({2, 2}));
  // Ordinary selection polling must keep the live surface, rather than bind
  // the authored surface over the completed first batch.
  live.bind("live", "terrain", liveJson, liveSurface);
  assert(live.surface() == firstPreview->surface);
  const auto secondPreview = nextBatch(live, error);
  assert(secondPreview && !secondPreview->final && batches == 2);
  assert(live.update({.focused = true, .leftReleased = true}, {}, error));
  const auto finalStroke = finish(live, error);
  assert(finalStroke && finalStroke->final && !live.stroking());
  assert(batches == 2 && fullGenerations == 0); // Release needs no extra job.
  assert(finalStroke->patch && !finalStroke->patch->samples.empty());
  assert(!finalStroke->patch->fullBefore && !finalStroke->patch->fullAfter);
  assert(!finalStroke->patch->invalidation.fullGeneration);
  assert(finalStroke->surface->heightField()->baseHeights ==
         liveSurface->heightField()->baseHeights);
  const auto undoSurface = editor::applyEditorTerrainPatch(
      finalStroke->surface, *finalStroke->patch, false, error);
  assert(undoSurface && undoSurface->heightField()->heights ==
                            liveSurface->heightField()->heights);
  const auto replay = runtime::TerrainGenerator::generate(
      runtime::TerrainRecipe::parse(finalStroke->recipe));
  assert(replay &&
         replay->heights == finalStroke->surface->heightField()->heights);

  // Cancel after live publication restores the exact original field and emits
  // a restoration event instead of an authored completion.
  live.bind("live", "terrain", finalStroke->recipe, finalStroke->surface);
  const auto beforeLiveCancel = live.surface();
  assert(live.update(
      {.hovered = true, .focused = true, .leftPressed = true, .leftDown = true},
      runtime::Vec3{10, 0, 10}, error));
  const auto cancelledPreview = nextBatch(live, error);
  assert(cancelledPreview && !cancelledPreview->final);
  assert(live.update({.focused = true, .cancelPressed = true}, {}, error));
  const auto restoration = live.takeRollback();
  assert(restoration && restoration->restore && !restoration->final);
  assert(restoration->surface == beforeLiveCancel);
  assert(!live.pendingEdits() && live.draft() == finalStroke->recipe);

  // Only enabled layers of the correct kind can receive stamps. IDs and
  // names remain distinct, and toggling keeps previously authored strokes.
  auto layered = liveJson;
  layered["layers"].push_back({{"id", "fine_sculpt"},
                               {"name", "Fine sculpt"},
                               {"kind", "sculpt"},
                               {"enabled", true}});
  auto layeredSurface = editor::generateEditorTerrain(layered, {}, {}, error);
  live.bind("layers", "terrain", layered, layeredSurface);
  live.brush.mode = editor::EditorTerrainBrush::Raise;
  live.brush.layer = "fine_sculpt";
  assert(live.update(
      {.hovered = true, .focused = true, .leftPressed = true, .leftDown = true},
      runtime::Vec3{5, 0, 5}, error));
  assert(live.update({.focused = true, .leftReleased = true}, {}, error));
  const auto layerStroke = finish(live, error);
  assert(layerStroke &&
         layerStroke->recipe["edits"].back()["layer"] == "fine_sculpt");
  layered = layerStroke->recipe;
  layered["layers"].back()["enabled"] = false;
  layeredSurface = editor::generateEditorTerrain(layered, {}, {}, error);
  live.bind("layers", "terrain", layered, layeredSurface);
  assert(!live.update(
      {.hovered = true, .focused = true, .leftPressed = true, .leftDown = true},
      runtime::Vec3{5, 0, 5}, error));
  assert(!live.stroking() && !error.empty());
  error.clear();
  live.brush.mode = editor::EditorTerrainBrush::Exclusion;
  live.brush.layer = "exclusions";
  live.brush.radius = 2;
  live.previewExclusions = true;
  assert(live.update(
      {.hovered = true, .focused = true, .leftPressed = true, .leftDown = true},
      runtime::Vec3{8, 0, 8}, error));
  assert(live.update({.focused = true, .leftReleased = true}, {}, error));
  const auto maskStroke = finish(live, error);
  assert(maskStroke && maskStroke->recipe["exclusions"].size() == 1);
  assert(maskStroke->surface->heightField()->heights ==
         layeredSurface->heightField()->heights);
  assert(maskStroke->surface->heightField()->exclusions !=
         layeredSurface->heightField()->exclusions);
  live.bind("layers", "terrain", maskStroke->recipe, maskStroke->surface);
  assert(!live.exclusionPreview().empty());
  live.brush.exclusionValue = 0;
  live.brush.radius = .5F;
  assert(live.update(
      {.hovered = true, .focused = true, .leftPressed = true, .leftDown = true},
      runtime::Vec3{8, 0, 8}, error));
  assert(live.update({.focused = true, .leftReleased = true}, {}, error));
  const auto erased = finish(live, error);
  assert(erased && erased->recipe["exclusions"].back()["value"] == 0);
  const auto mask = erased->surface->heightField();
  assert(mask->exclusions[mask->index(8, 8)] == 0);
  assert(mask->exclusions[mask->index(9, 8)] > 0);
  assert(mask->heights == maskStroke->surface->heightField()->heights);
  assert(erased->patch->invalidation.geometrySamples().empty());

  // Biome rules are draft settings: Generate carries them into the commit, and
  // the same commit keeps the previously authored recipe as its undo source.
  runtime::TerrainRecipe ruleBase;
  ruleBase.size = {32, 24};
  ruleBase.cellsX = 32;
  ruleBase.cellsZ = 24;
  ruleBase.landforms["default"].heightVariation = 6;
  ruleBase.landforms["default"].baseHeight = 4;
  ruleBase.landforms["peak"] = runtime::TerrainLandform{
      .baseHeight = 12, .heightVariation = 2, .featureSize = 4};
  ruleBase.biomes["peak"] =
      runtime::TerrainBiome{.landform = "peak", .color = {.8F, .85F, .9F, 1}};
  const auto baseJson = ruleBase.toJson();
  auto ruled = runtime::TerrainRecipe::parse(baseJson);
  ruled.rules.push_back(
      runtime::TerrainBiomeRule{.id = "peaks",
                                .layer = "biomes",
                                .biome = "peak",
                                .priority = 10,
                                .elevation = {true, 6.0F, 14.0F}});
  const auto ruledJson = ruled.toJson();
  const auto baseSurface =
      editor::generateEditorTerrain(baseJson, {}, {}, error);
  const auto ruledSurface =
      editor::generateEditorTerrain(ruledJson, {}, {}, error);
  assert(baseSurface && ruledSurface && error.empty());
  editor::EditorTerrainAuthoring rules(
      [surface](const auto &, std::stop_token,
                const std::function<void(float)> &progress, std::string &) {
        progress(1);
        return surface;
      });
  rules.bind("rules", "terrain", baseJson, baseSurface);
  rules.draft() = ruledJson;
  assert(rules.generate(std::nullopt, error));
  const auto ruleCommit = finish(rules, error);
  assert(ruleCommit && ruleCommit->recipe["rules"].size() == 1);
  assert(ruleCommit->before == baseJson);
  const auto committedRule =
      runtime::TerrainRecipe::parse(ruleCommit->recipe).rules.front();
  assert(committedRule.id == "peaks" && committedRule.biome == "peak" &&
         committedRule.layer == "biomes" && committedRule.priority == 10);
  assert(committedRule.elevation.enabled &&
         committedRule.elevation.minimum == 6 &&
         committedRule.elevation.maximum == 14);
  assert(!committedRule.slope.enabled && !committedRule.moisture.enabled &&
         !committedRule.waterDistance.enabled);

  // An invalid rule is rejected before the worker thread starts, so it never
  // becomes a pending commit and never reaches the generator.
  const auto reject = [&](nlohmann::json invalid, std::string_view expected) {
    editor::EditorTerrainAuthoring authoring(
        [](const auto &, std::stop_token, const std::function<void(float)> &,
           std::string &) -> editor::EditorTerrainSurfacePtr {
          throw std::runtime_error("a rejected rule reached the generator");
        });
    authoring.bind("invalid", "terrain", baseJson, baseSurface);
    authoring.draft()["rules"] = std::move(invalid);
    assert(!authoring.needsResizeDecision());
    std::string message;
    assert(!authoring.generate(std::nullopt, message) && !message.empty());
    assert(message.find(expected) != std::string::npos);
    assert(!authoring.busy() && !authoring.pendingEdits());
  };
  reject(nlohmann::json::array(
             {{{"id", "ghost"}, {"biome", "missing"}, {"layer", "biomes"}}}),
         "unknown biome");
  reject(nlohmann::json::array(
             {{{"id", "wrong_kind"}, {"biome", "peak"}, {"layer", "sculpt"}}}),
         "layer kind");
  reject(nlohmann::json::array({{{"id", "soaked"},
                                 {"biome", "peak"},
                                 {"layer", "biomes"},
                                 {"blend", -1.0F}}}),
         "blend");
  reject(nlohmann::json::array(
             {{{"id", "inverted"},
               {"biome", "peak"},
               {"layer", "biomes"},
               {"elevation", nlohmann::json::array({4.0F, 1.0F})}}}),
         "minimum must not exceed");
  reject(nlohmann::json::array(
             {{{"id", ""}, {"biome", "peak"}, {"layer", "biomes"}}}),
         "id must not be empty");
  // A rule on a disabled biome layer is valid but never evaluated.
  auto disabled = runtime::TerrainRecipe::parse(baseJson);
  disabled.layers.at(1).enabled = false;
  const auto disabledJson = disabled.toJson();
  const auto disabledSurface =
      editor::generateEditorTerrain(disabledJson, {}, {}, error);
  assert(disabledSurface && error.empty());
  editor::EditorTerrainAuthoring silenced(
      [surface](const auto &, std::stop_token,
                const std::function<void(float)> &progress, std::string &) {
        progress(1);
        return surface;
      });
  silenced.bind("disabled", "terrain", disabledJson, disabledSurface);
  silenced.draft()["rules"] = ruledJson["rules"];
  assert(!silenced.needsResizeDecision());
  assert(silenced.generate(std::nullopt, error));
  const auto silencedCommit = finish(silenced, error);
  assert(silencedCommit && silencedCommit->recipe["rules"].size() == 1);
  assert(silencedCommit->recipe["layers"][1]["enabled"] == false);
  assert(
      std::ranges::all_of(silencedCommit->surface->heightField()->biomeIndices,
                          [](std::size_t index) { return index == 0; }));

  // Rules are grid independent, so a rules-only change never asks how to
  // preserve protection snapshots against a resized grid.
  editor::EditorTerrainAuthoring resizing;
  auto stamped = baseJson;
  nlohmann::json stampedRegion{{"biome", "peak"},
                               {"center", nlohmann::json::array({8, 8})},
                               {"radius", 4}};
  stamped["regions"].push_back(stampedRegion);
  resizing.bind("resize", "terrain", stamped, baseSurface);
  resizing.draft()["rules"] = ruledJson["rules"];
  assert(!resizing.needsResizeDecision());
  resizing.draft()["resolution"] = {48, 48};
  assert(resizing.needsResizeDecision());

  // The mask overlay reads the committed field, so a draft rule edit cannot
  // move it before Generate runs.
  editor::EditorTerrainAuthoring overlay;
  overlay.bind("overlay", "terrain", baseJson, baseSurface);
  overlay.draft() = ruledJson;
  assert(overlay.hasDraftChanges());
  const auto committedBiome =
      overlay.ruleMaskPreview(editor::EditorTerrainMaskPreview::Biome, 512);
  assert(!committedBiome.empty());
  assert(std::ranges::all_of(
      committedBiome, [](const auto &sample) { return sample.biome == 0; }));
  const auto committedField = baseSurface->heightField();
  assert(committedBiome.front().color.r == committedField->biomeColors[0].r);
  editor::EditorTerrainAuthoring evaluated;
  evaluated.bind("evaluated", "terrain", ruledJson, ruledSurface);
  const auto evaluatedBiome =
      evaluated.ruleMaskPreview(editor::EditorTerrainMaskPreview::Biome, 512);
  assert(std::ranges::any_of(
      evaluatedBiome, [](const auto &sample) { return sample.biome != 0; }));

  // Sampling never exceeds the caller's screen budget, and every weight is a
  // finite fraction of its own range.
  assert(overlay.ruleMaskPreview(editor::EditorTerrainMaskPreview::None, 512)
             .empty());
  assert(
      overlay.ruleMaskPreview(editor::EditorTerrainMaskPreview::None).empty());
  for (const auto mode : {editor::EditorTerrainMaskPreview::Biome,
                          editor::EditorTerrainMaskPreview::Elevation,
                          editor::EditorTerrainMaskPreview::Slope}) {
    assert(overlay.ruleMaskPreview(mode, 1).size() <= 1);
    assert(overlay.ruleMaskPreview(mode, 64).size() <= 64);
    for (const auto &sample : overlay.ruleMaskPreview(mode, 256)) {
      assert(std::isfinite(sample.weight) && sample.weight >= 0.0F &&
             sample.weight <= 1.0F);
    }
  }
  const auto *field = committedField.get();
  const auto [lowest, highest] =
      std::minmax_element(field->heights.begin(), field->heights.end());
  assert(*highest > *lowest);
  for (const auto &sample : overlay.ruleMaskPreview(
           editor::EditorTerrainMaskPreview::Elevation, 4096)) {
    const float expected =
        std::clamp((sample.position.y - .04F - *lowest) / (*highest - *lowest),
                   0.0F, 1.0F);
    assert(std::abs(sample.weight - expected) < 0.0001F);
  }

  // A flat field has no elevation range; the preview reports zero instead of
  // dividing by it.
  runtime::TerrainRecipe flat;
  flat.size = {16, 16};
  flat.cellsX = 8;
  flat.cellsZ = 8;
  flat.landforms.at("default").heightVariation = 0;
  const auto flatSurface =
      editor::generateEditorTerrain(flat.toJson(), {}, {}, error);
  assert(flatSurface && error.empty());
  editor::EditorTerrainAuthoring degenerate;
  degenerate.bind("flat", "terrain", flat.toJson(), flatSurface);
  const auto flatMask = degenerate.ruleMaskPreview(
      editor::EditorTerrainMaskPreview::Elevation, 64);
  assert(!flatMask.empty() && flatMask.size() <= 64);
  assert(std::ranges::all_of(
      flatMask, [](const auto &sample) { return sample.weight == 0.0F; }));
  // None is the only mode a surface without a field can answer.
  editor::EditorTerrainAuthoring fieldless;
  fieldless.bind("fieldless", "terrain", recipe, surface);
  for (const auto mode : {editor::EditorTerrainMaskPreview::None,
                          editor::EditorTerrainMaskPreview::Biome,
                          editor::EditorTerrainMaskPreview::Elevation,
                          editor::EditorTerrainMaskPreview::Slope})
    assert(fieldless.ruleMaskPreview(mode, 64).empty());

  // Landscape presets reach the editor through a transient cache. Applying one
  // replaces generation keys only: the author's painting and sculpting are
  // never templated away.
  editor::EditorTerrainAuthoring landscapes;
  const auto alpine = preset();
  landscapes.setPresets(
      {alpine, preset("asset://terrain/presets/rolling", 77)});
  assert(landscapes.presets().size() == 2);
  assert(landscapes.preset("asset://terrain/presets/rolling"));
  assert(landscapes.preset("asset://terrain/presets/mountain") == nullptr);
  // The cache is per binding: it is editor state, not per-terrain state.
  landscapes.bind("preset-scene", "terrain", recipe, surface);
  assert(landscapes.presets().size() == 2);
  assert(landscapes.entityId() == "terrain");

  auto painted = recipe;
  painted["regions"] = nlohmann::json::array(
      {{{"biome", "scree"}, {"center", {8, 8}}, {"radius", 4}}});
  painted["edits"] = nlohmann::json::array({{{"type", "flatten"},
                                             {"center", {12, 12}},
                                             {"radius", 4},
                                             {"target_height", 2}}});
  landscapes.bind("preset-scene", "terrain", painted, surface);
  const auto beforeApply = landscapes.draft();
  assert(landscapes.applyPreset(alpine, error));
  const auto applied = landscapes.draft();
  assert(applied["regions"] == beforeApply["regions"]);
  assert(applied["edits"] == beforeApply["edits"]);
  assert(applied["seed"] == 4242 && applied["default_biome"] == "rock");
  assert(applied["biomes"].size() == 2 && applied["layers"].size() > 0);
  assert(applied["preset_id"] == "asset://terrain/presets/alpine");
  assert(applied["preset_version"] == 1);
  // A region painted with a biome only the preset supplies is valid afterwards.
  assert(runtime::TerrainRecipe::parse(applied).regions.size() == 1);
  assert(landscapes.hasDraftChanges());
  assert(landscapes.brush.biome == "default" ||
         landscapes.brush.biome == "rock");

  // Re-applying the same preset changes nothing, so it can never become a
  // spurious undoable edit.
  assert(landscapes.applyPreset(alpine, error));
  assert(landscapes.draft() == applied);
  assert(landscapes.applyPreset(preset("asset://terrain/presets/rolling", 77),
                                error));
  assert(landscapes.draft()["seed"] == 77);
  assert(landscapes.draft()["preset_id"] == "asset://terrain/presets/rolling");

  // Clearing removes both provenance keys together and leaves a valid recipe.
  assert(landscapes.clearPreset(error));
  assert(!landscapes.draft().contains("preset_id"));
  assert(!landscapes.draft().contains("preset_version"));
  assert(runtime::TerrainRecipe::parse(landscapes.draft()).presetId.empty());
  const auto withoutProvenance = landscapes.draft();
  assert(landscapes.clearPreset(error));
  assert(landscapes.draft() == withoutProvenance);

  // A rejected merge leaves the draft alone and reports the engine's message
  // verbatim, rather than committing a half-applied landscape.
  auto orphanStroke = recipe;
  orphanStroke["regions"] = nlohmann::json::array(
      {{{"biome", "nowhere"}, {"center", {8, 8}}, {"radius", 4}}});
  landscapes.bind("preset-scene", "terrain", orphanStroke, surface);
  const auto beforeReject = landscapes.draft();
  assert(!landscapes.applyPreset(alpine, error));
  assert(error.find("unknown biome") != std::string::npos);
  assert(landscapes.draft() == beforeReject);
  error.clear();

  // A preset that cannot be validated is refused at the same boundary.
  auto broken = alpine;
  broken.rules.push_back(runtime::TerrainBiomeRule{
      .id = "ghost", .layer = "biomes", .biome = "missing", .priority = 1});
  landscapes.bind("preset-scene", "terrain", recipe, surface);
  assert(!landscapes.applyPreset(broken, error));
  assert(error.find("unknown biome") != std::string::npos);
  assert(landscapes.draft() == recipe);
  error.clear();

  // Protection snapshots belong to one grid, so a preset that would move it is
  // refused by the merged-recipe validation itself. Nothing is invalidated and
  // the author is told what the engine says.
  auto protectedStrokes = recipe;
  protectedStrokes["edits"] =
      nlohmann::json::array({surface->protection({8, 8}, 2, 1, 0)});
  const auto rescaled =
      preset("asset://terrain/presets/alpine", 4242, {16, 16}, 4);
  landscapes.bind("preset-scene", "terrain", protectedStrokes, surface);
  assert(landscapes.presetChangesGrid(rescaled));
  assert(!landscapes.presetChangesGrid(alpine));
  const auto beforeResized = landscapes.draft();
  assert(!landscapes.applyPreset(rescaled, error));
  assert(error.find("protection") != std::string::npos);
  assert(landscapes.draft() == beforeResized);
  assert(!landscapes.needsResizeDecision());
  error.clear();

  // Without protection, a grid-changing preset merges and the existing resize
  // decision answers for it, so strokes are resampled only on an explicit
  // choice.
  auto sculpted = recipe;
  sculpted["edits"] = nlohmann::json::array({{{"type", "flatten"},
                                              {"center", {8, 8}},
                                              {"radius", 4},
                                              {"target_height", 2}}});
  landscapes.bind("preset-scene", "terrain", sculpted, surface);
  assert(landscapes.presetChangesGrid(rescaled));
  assert(landscapes.applyPreset(rescaled, error));
  assert(landscapes.needsResizeDecision());
  assert(landscapes.draft()["edits"] == sculpted["edits"]);
  assert(landscapes.draft()["resolution"] == nlohmann::json::array({4, 4}));
  assert(!landscapes.generate(std::nullopt, error));
  assert(error == "Choose how to preserve terrain edits after resizing.");
  error.clear();
  assert(landscapes.generate(editor::EditorTerrainResize::Keep, error));
  const auto resampled = finish(landscapes, error);
  assert(resampled);
  assert(resampled->recipe["edits"].size() == 1);
  assert(resampled->recipe["preset_id"] == rescaled.id);
  assert(resampled->before == sculpted);

  // Presets refuse to touch a busy or unbound authoring object, so an apply
  // can never race a worker or land on a different terrain than the one shown.
  editor::EditorTerrainAuthoring busyPresets(
      [surface](const auto &, std::stop_token,
                const std::function<void(float)> &progress, std::string &) {
        progress(1);
        return surface;
      });
  busyPresets.setPresets({alpine});
  assert(!busyPresets.applyPreset(alpine, error));
  assert(error == "Terrain tools require the 3D scene stage.");
  error.clear();
  busyPresets.bind("busy", "terrain", recipe, surface);
  assert(busyPresets.applyPreset(alpine, error));
  busyPresets.draft()["seed"] = 4243;
  assert(busyPresets.generate(std::nullopt, error));
  assert(!busyPresets.applyPreset(alpine, error));
  assert(error == "Finish the current terrain operation first.");
  error.clear();
  // The refused apply started nothing: the only commit is the pending one.
  const auto committed = finish(busyPresets, error);
  assert(committed);
  assert(committed->recipe["seed"] == 4243);
  assert(committed->recipe["preset_id"] == alpine.id);
  assert(!finish(busyPresets, error));
}
