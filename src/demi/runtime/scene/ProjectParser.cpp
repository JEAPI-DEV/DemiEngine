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
