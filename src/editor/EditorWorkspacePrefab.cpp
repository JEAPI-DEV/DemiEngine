#include "editor/EditorWorkspace.h"

#include "demi/filesystem/ProjectPaths.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"

#include <algorithm>

namespace demi::editor {

bool EditorWorkspace::instantiatePrefab(const std::filesystem::path &path,
                                        std::string &error) {
  if (!project_ || activeDocument_ != EditorWorkspaceDocument::Scene) {
    error = "Open a scene or entity prefab before placing an entity prefab.";
    return false;
  }
  const auto root = std::filesystem::weakly_canonical(
      project_->project.projectDirectory / "prefabs");
  const auto source = std::filesystem::weakly_canonical(path);
  const auto relative = source.lexically_relative(root);
  if (!isPrefabFile(source) || relative.empty() || relative.is_absolute() ||
      *relative.begin() == "..") {
    error = "Choose an entity prefab from this project's prefabs folder.";
    return false;
  }
  std::string reference = relative.generic_string();
  reference.resize(reference.size() - std::string_view(".prefab.json").size());
  reference = "prefab://" + reference;
  if (!mutateAndRebuild(
          [&reference](EditorSceneDocument &document, std::string &failure) {
            return document.instantiatePrefab(reference, failure);
          },
          error))
    return false;

  const std::string instanceId(sceneDocument_.lastChangedEntityId());
  const auto instance = std::ranges::find_if(
      project_->world.entities, [&instanceId](const runtime::Entity &entity) {
        return entity.prefabInstance == instanceId;
      });
  if (instance != project_->world.entities.end()) {
    selectEntity(instance->id);
    (void)sceneView_.frameEntity(project_->world, instance->id);
  }
  return true;
}

bool EditorWorkspace::openPrefabDocument(const std::filesystem::path &path,
                                         std::string &error) {
  if (!isPrefabFile(path)) {
    error = "Select a scene prefab (*.prefab.json).";
    return false;
  }
  return openEntityDocument(path, true, error);
}

bool EditorWorkspace::removePrefabInstance(
    const std::string_view expandedEntityId, std::string &error) {
  const auto origin =
      authoredTarget({.entityId = std::string(expandedEntityId)});
  if (!origin.isPrefabOverride()) {
    error = "Select an entity belonging to a prefab instance.";
    return false;
  }
  return deleteEntity(origin.prefabInstanceId, error);
}

bool EditorWorkspace::duplicatePrefabInstance(
    const std::string_view expandedEntityId, std::string &error) {
  const auto origin =
      authoredTarget({.entityId = std::string(expandedEntityId)});
  if (!origin.isPrefabOverride()) {
    error = "Select an entity belonging to a prefab instance.";
    return false;
  }
  if (!duplicateEntity(origin.prefabInstanceId, error))
    return false;
  const std::string instanceId(sceneDocument_.lastChangedEntityId());
  const auto entity = std::ranges::find_if(
      project_->world.entities, [&instanceId](const runtime::Entity &item) {
        return item.prefabInstance == instanceId;
      });
  if (entity != project_->world.entities.end())
    selectEntity(entity->id);
  return true;
}

std::optional<runtime::World>
EditorWorkspace::loadEntityPreview(const EditorSceneDocument &document,
                                   const bool prefab,
                                   std::string &error) const {
  if (!prefab) {
    const auto entry = std::ranges::find_if(
        project_->project.scenes, [&](const runtime::SceneEntry &candidate) {
          return std::filesystem::absolute(project_->project.projectDirectory /
                                           candidate.path)
                     .lexically_normal() == document.path();
        });
    if (entry == project_->project.scenes.end()) {
      error = "The active scene is no longer registered in the project.";
      return std::nullopt;
    }
    return runtime::loadSceneDocument(project_->project, entry->id,
                                      document.json(), error, false);
  }

  // A transient scene entry supplies source-relative resolution to the normal
  // loader. Neither the project nor the prefab receives preview-only fields.
  auto previewProject = project_->project;
  const std::string previewId = "scene://editor-prefab-preview";
  previewProject.scenes = {{.id = previewId, .path = document.path()}};
  auto preview = document.json();
  return runtime::loadSceneDocument(previewProject, previewId, preview, error,
                                    false);
}

} // namespace demi::editor
