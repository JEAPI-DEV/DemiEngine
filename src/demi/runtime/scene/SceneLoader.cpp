#include "demi/runtime/scene/SceneData.h"

#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/HudParser.h"
#include "demi/runtime/scene/ProjectParser.h"
#include "demi/runtime/scene/SceneEntityParser.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/schema/Validation.h"

#include <algorithm>

namespace demi::runtime {

namespace {

[[nodiscard]] bool validateLoadPath(const std::filesystem::path &path,
                                    const char *label, std::string &error) {
  const ValidationSummary validation = validatePath(path);
  if (!hasErrors(validation.diagnostics)) {
    return true;
  }

  error = std::string(label) + " validation failed before runtime load";
  if (!path.empty()) {
    error += ": " + path.string();
  }
  return false;
}

[[nodiscard]] const SceneEntry *findSceneEntry(const ProjectData &project,
                                               const std::string &sceneId) {
  const auto scene =
      std::ranges::find_if(project.scenes, [&](const SceneEntry &entry) {
        return entry.id == sceneId;
      });
  return scene == project.scenes.end() ? nullptr : &*scene;
}

} // namespace

std::optional<LoadedProject>
loadProject(const std::filesystem::path &projectPath, std::string &error) {
  if (!validateLoadPath(projectPath, "Project", error)) {
    return std::nullopt;
  }

  const std::optional<scene_loading::Json> projectJson =
      scene_loading::readJsonFile(projectPath, error);
  if (!projectJson.has_value()) {
    return std::nullopt;
  }

  std::optional<ProjectData> project =
      scene_loading::parseProjectData(projectPath, *projectJson, error);
  if (!project.has_value()) {
    return std::nullopt;
  }

  if (findSceneEntry(*project, project->mainScene) == nullptr) {
    error = "main_scene does not match any scene entry: " + project->mainScene;
    return std::nullopt;
  }

  std::optional<World> world = loadScene(*project, project->mainScene, error);
  if (!world.has_value()) {
    return std::nullopt;
  }

  return LoadedProject{.project = *project, .world = std::move(*world)};
}

std::optional<World> loadScene(const ProjectData &project,
                               const std::string &sceneId, std::string &error) {
  const SceneEntry *scene = findSceneEntry(project, sceneId);
  if (scene == nullptr) {
    error = "No scene registered with id: " + sceneId;
    return std::nullopt;
  }

  const std::filesystem::path scenePath =
      project.projectDirectory / scene->path;
  if (!validateLoadPath(scenePath, "Scene", error)) {
    return std::nullopt;
  }

  const std::optional<scene_loading::Json> sceneJson =
      scene_loading::readJsonFile(scenePath, error);
  if (!sceneJson.has_value()) {
    return std::nullopt;
  }

  const composition::ExpansionResult expansion =
      composition::expandScene(scenePath, *sceneJson);
  if (!expansion.document.has_value()) {
    error = expansion.diagnostics.empty()
                ? "Scene prefab expansion failed: " + scenePath.string()
                : expansion.diagnostics.front().message;
    return std::nullopt;
  }

  World world = scene_loading::parseSceneWorld(scenePath, *expansion.document);
  world.debug = project.debug;
  const std::size_t layerCount =
      std::min<std::size_t>(project.physicsLayers2D.size(), 16);
  for (std::size_t index = 0; index < layerCount; ++index) {
    world.physicsCategoryBits[project.physicsLayers2D[index].name] =
        static_cast<std::uint16_t>(1U << index);
  }
  for (std::size_t index = 0; index < layerCount; ++index) {
    std::uint16_t mask = 0;
    for (const std::string &target :
         project.physicsLayers2D[index].collidesWith) {
      if (const auto found = world.physicsCategoryBits.find(target);
          found != world.physicsCategoryBits.end())
        mask = static_cast<std::uint16_t>(mask | found->second);
    }
    world.physicsMaskBits[project.physicsLayers2D[index].name] = mask;
  }
  if (const std::optional<std::string> hud =
          scene_loading::stringField(*sceneJson, "hud")) {
    const std::filesystem::path hudPath = scenePath.parent_path() / *hud;
    if (!validateLoadPath(hudPath, "HUD", error)) {
      return std::nullopt;
    }
    scene_loading::loadHudFile(world, hudPath, error);
    if (!error.empty()) {
      return std::nullopt;
    }
  }

  return world;
}

} // namespace demi::runtime
