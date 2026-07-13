#include "demi/runtime/scene/ProjectParser.h"
#include "demi/runtime/input/InputActionParser.h"

#include <algorithm>

namespace demi::runtime::scene_loading {

std::optional<ProjectData>
parseProjectData(const std::filesystem::path &projectPath, const Json &document,
                 std::string &error) {
  ProjectData project;
  project.projectPath = projectPath;
  project.projectDirectory = projectPath.parent_path();

  const std::optional<std::string> name = stringField(document, "name");
  const std::optional<std::string> mainScene =
      stringField(document, "main_scene");
  const Json *scenes = arrayField(document, "scenes");
  if (!name.has_value() || !mainScene.has_value() || scenes == nullptr) {
    error = "Project file is missing name, main_scene, or scenes.";
    return std::nullopt;
  }

  project.name = *name;
  project.mainScene = *mainScene;
  project.inputActions = input::parseInputActions(document);
  project.scriptEntry = stringOr(document, "entry");

  if (const Json *simulation = objectField(document, "simulation")) {
    project.simulation.fixedTimestep = std::clamp(
        numberField(*simulation, "fixed_timestep").value_or(1.0F / 60.0F),
        1.0F / 1000.0F, 1.0F);
    project.simulation.randomSeed = static_cast<std::uint64_t>(
        std::max(numberField(*simulation, "random_seed").value_or(1.0F), 1.0F));
  }

  if (const Json *budgets = objectField(document, "performance_budgets")) {
    project.performanceBudgets.maximumFrameMilliseconds = std::max(
        numberField(*budgets, "maximum_frame_ms").value_or(16.67F), 0.1F);
    project.performanceBudgets.maximumDrawCalls = static_cast<int>(std::max(
        numberField(*budgets, "maximum_draw_calls").value_or(500.0F), 1.0F));
    project.performanceBudgets.maximumResidentAssets =
        static_cast<int>(std::max(
            numberField(*budgets, "maximum_resident_assets").value_or(256.0F),
            1.0F));
  }

  if (const Json *debug = objectField(document, "debug")) {
    project.debug.colliders = boolField(*debug, "colliders").value_or(false);
    project.debug.contacts = boolField(*debug, "contacts").value_or(false);
    project.debug.grid = boolField(*debug, "grid").value_or(false);
    project.debug.entityIds = boolField(*debug, "entity_ids").value_or(false);
    project.debug.drawOrder = boolField(*debug, "draw_order").value_or(false);
    project.debug.uiBounds = boolField(*debug, "ui_bounds").value_or(false);
    project.debug.profilerHud =
        boolField(*debug, "profiler_hud").value_or(false);
  }

  if (const Json *physics = objectField(document, "physics")) {
    if (const Json *layers = objectField(*physics, "layers")) {
      for (const auto &[name, targets] : layers->items()) {
        PhysicsLayer2D layer;
        layer.name = name;
        if (targets.is_array()) {
          for (const Json &target : targets)
            if (target.is_string())
              layer.collidesWith.push_back(target.get<std::string>());
        }
        std::ranges::sort(layer.collidesWith);
        project.physicsLayers2D.push_back(std::move(layer));
      }
      std::ranges::sort(project.physicsLayers2D, {}, &PhysicsLayer2D::name);
    }
  }

  if (const Json *scripting = objectField(document, "scripting")) {
    project.scriptEntry = stringOr(*scripting, "entry", project.scriptEntry);
    if (const Json *modules = arrayField(*scripting, "modules")) {
      for (const Json &module : *modules) {
        if (module.is_string()) {
          project.scriptModules.push_back(module.get<std::string>());
        }
      }
    }
  }

  for (const Json &scene : *scenes) {
    const std::optional<std::string> id = stringField(scene, "id");
    const std::optional<std::string> path = stringField(scene, "path");
    if (id.has_value() && path.has_value()) {
      project.scenes.push_back(SceneEntry{.id = *id, .path = *path});
    }
  }

  if (project.scenes.empty()) {
    error = "Project does not contain any loadable scenes.";
    return std::nullopt;
  }

  return project;
}

} // namespace demi::runtime::scene_loading
