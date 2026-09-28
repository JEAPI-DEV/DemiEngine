#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
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
finish(editor::EditorTerrainAuthoring &authoring, std::string &error) {
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
}
