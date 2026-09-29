#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "editor/EditorTerrainAuthoring.h"
#include "editor/EditorTerrainPicking.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
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
} // namespace

int main() {
  using namespace demi;
  const auto surface = std::make_shared<TestSurface>();
  const auto recipe = runtime::TerrainRecipe::defaults();
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
  assert(stroke && stroke->recipe["edits"].size() >= 2);
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
  elevatedRecipe.biomes["default"].baseHeight = 12;
  elevatedRecipe.biomes["default"].heightVariation = 0;
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
  liveRecipe.biomes.at("default").heightVariation = 0;
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
}
