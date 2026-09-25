#include "editor/EditorWorkspace.h"

#include "demi/filesystem/ProjectPaths.h"

#include <algorithm>

namespace demi::editor {

bool EditorWorkspace::openPrefabDocument(const std::filesystem::path &path,
                                         std::string &error) {
  if (!isPrefabFile(path)) {
    error = "Select a scene prefab (*.prefab.json).";
    return false;
  }
  return openEntityDocument(path, true, error);
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
    return runtime::loadSceneDocument(project_->project, entry->id, document.json(), error, false);
  }

  // A transient scene entry supplies source-relative resolution to the normal
  // loader. Neither the project nor the prefab receives preview-only fields.
  auto previewProject = project_->project;
  const std::string previewId = "scene://editor-prefab-preview";
  previewProject.scenes = {{.id = previewId, .path = document.path()}};
  auto preview = document.json();
  preview["id"] = previewId;
  return runtime::loadSceneDocument(previewProject, previewId, preview, error, false);
}

} // namespace demi::editor
