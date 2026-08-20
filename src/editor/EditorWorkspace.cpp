#include "editor/EditorWorkspace.h"

#include "demi/runtime/scene/SceneEntityParser.h"
#include "demi/schema/Validation.h"

#include <algorithm>

namespace demi::editor {

bool EditorWorkspace::open(std::filesystem::path projectPath,
                           std::string &error) {
  if (std::filesystem::is_directory(projectPath))
    projectPath /= "demi.project.json";
  projectPath = std::filesystem::absolute(projectPath).lexically_normal();

  auto loaded = runtime::loadProject(projectPath, error);
  if (!loaded)
    return false;

  projectPath_ = std::move(projectPath);
  project_ = std::move(loaded);
  if (!sceneDocument_.open(project_->world.scenePath, error)) {
    project_.reset();
    return false;
  }
  sceneView_.reset(project_->world);
  viewportTool_.cancelDrag();
  discoverSources();
  if (!project_->world.entities.empty())
    selectedEntityId_ = project_->world.entities.front().id;
  refreshDiagnostics();
  return true;
}

void EditorWorkspace::discoverSources() {
  sources_.clear();
  std::error_code filesystemError;
  std::filesystem::recursive_directory_iterator iterator(
      project_->project.projectDirectory,
      std::filesystem::directory_options::skip_permission_denied,
      filesystemError);
  const std::filesystem::recursive_directory_iterator end;
  for (; !filesystemError && iterator != end;
       iterator.increment(filesystemError)) {
    if (iterator->is_directory()) {
      const std::string name = iterator->path().filename().string();
      if (name == "generated" || name == "build" || name == ".demi" ||
          name == ".git")
        iterator.disable_recursion_pending();
      continue;
    }
    if (iterator->is_regular_file())
      sources_.push_back(iterator->path());
  }
  std::ranges::sort(sources_);
}

bool EditorWorkspace::refresh(std::string &error) {
  if (sceneDocument_.isDirty()) {
    error = "The scene has unsaved changes. Save or undo them before "
            "refreshing.";
    return false;
  }
  auto loaded = runtime::loadProject(projectPath_, error);
  if (!loaded)
    return false;
  if (!sceneDocument_.reload(error))
    return false;
  project_ = std::move(loaded);
  viewportTool_.cancelDrag();
  discoverSources();
  if (!selectedEntityId_.empty() && selectedEntity() == nullptr)
    selectedEntityId_.clear();
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::save(std::string &error) {
  if (!sceneDocument_.save(error)) {
    syncEditorDiagnostic();
    return false;
  }
  auto loaded = runtime::loadProject(projectPath_, error);
  if (!loaded)
    return false;
  project_ = std::move(loaded);
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::resolveExternalChange(
    const ExternalChangeDecision decision,
    const std::filesystem::path &copyPath, std::string &error) {
  EditorSceneDocument before = sceneDocument_;
  if (!sceneDocument_.resolveExternalChange(decision, copyPath, error))
    return false;
  if (decision == ExternalChangeDecision::ReloadFromDisk) {
    auto loaded = runtime::loadProject(projectPath_, error);
    if (!loaded) {
      sceneDocument_ = std::move(before);
      workspaceOperationError_ = error;
      syncEditorDiagnostic();
      return false;
    }
    project_ = std::move(loaded);
    viewportTool_.cancelDrag();
    discoverSources();
    if (!selectedEntityId_.empty() && selectedEntity() == nullptr)
      selectedEntityId_.clear();
  }
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::undo(std::string &error) {
  if (!mutateAndRebuild(
          [](EditorSceneDocument &document, std::string &mutationError) {
            return document.undo(mutationError);
          },
          error))
    return false;
  if (selectedEntity() == nullptr && !project_->world.entities.empty())
    selectedEntityId_ = project_->world.entities.front().id;
  return true;
}

bool EditorWorkspace::redo(std::string &error) {
  if (!mutateAndRebuild(
          [](EditorSceneDocument &document, std::string &mutationError) {
            return document.redo(mutationError);
          },
          error))
    return false;
  if (selectedEntity() == nullptr && !project_->world.entities.empty())
    selectedEntityId_ = project_->world.entities.front().id;
  return true;
}

bool EditorWorkspace::editValue(SceneValueTarget target, nlohmann::json value,
                                const bool continuous, std::string &error) {
  if (!sceneDocument_.setValue(std::move(target), std::move(value), continuous,
                               error)) {
    syncEditorDiagnostic();
    return false;
  }
  workspaceOperationError_.clear();
  syncChangedEntity();
  syncEditorDiagnostic();
  return true;
}

bool EditorWorkspace::removeValue(SceneValueTarget target, std::string &error) {
  return mutateAndRebuild(
      [target = std::move(target)](EditorSceneDocument &document,
                                   std::string &mutationError) mutable {
        return document.removeValue(std::move(target), mutationError);
      },
      error);
}

bool EditorWorkspace::createEntity(std::string &error) {
  if (!mutateAndRebuild(
          [](EditorSceneDocument &document, std::string &mutationError) {
            return document.createEntity(mutationError);
          },
          error))
    return false;
  selectEntity(std::string(sceneDocument_.lastChangedEntityId()));
  return true;
}

bool EditorWorkspace::deleteEntity(const std::string_view id,
                                   std::string &error) {
  if (!mutateAndRebuild(
          [id = std::string(id)](EditorSceneDocument &document,
                                 std::string &mutationError) {
            return document.deleteEntity(id, mutationError);
          },
          error))
    return false;
  if (selectedEntity() == nullptr)
    selectedEntityId_.clear();
  return true;
}

bool EditorWorkspace::reparentEntity(const std::string_view id,
                                     std::optional<std::string> newParent,
                                     std::string &error) {
  return mutateAndRebuild(
      [id = std::string(id), newParent = std::move(newParent)](
          EditorSceneDocument &document, std::string &mutationError) mutable {
        return document.reparent(id, std::move(newParent), mutationError);
      },
      error);
}

bool EditorWorkspace::duplicateEntity(const std::string_view id,
                                      std::string &error) {
  if (!mutateAndRebuild(
          [id = std::string(id)](EditorSceneDocument &document,
                                 std::string &mutationError) {
            return document.duplicateEntity(id, mutationError);
          },
          error))
    return false;
  selectEntity(std::string(sceneDocument_.lastChangedEntityId()));
  return true;
}

bool EditorWorkspace::addComponent(const std::string_view id,
                                   const std::string_view componentName,
                                   std::string &error) {
  return mutateAndRebuild(
      [id = std::string(id), componentName = std::string(componentName)](
          EditorSceneDocument &document, std::string &mutationError) {
        return document.addComponent(id, componentName, mutationError);
      },
      error);
}

bool EditorWorkspace::removeComponent(const std::string_view id,
                                      const std::string_view componentName,
                                      std::string &error) {
  return mutateAndRebuild(
      [id = std::string(id), componentName = std::string(componentName)](
          EditorSceneDocument &document, std::string &mutationError) {
        return document.removeComponent(id, componentName, mutationError);
      },
      error);
}

bool EditorWorkspace::updateViewportTool(const EditorViewportToolInput &input,
                                         std::string &error) {
  EditorViewportToolAction action = viewportTool_.update(
      project_->world, selectedEntityId_, sceneView_, input);
  if (action.selectionChanged)
    selectEntity(std::move(action.selectedEntityId));

  if (action.edit && !editValue(std::move(action.edit->target),
                                std::move(action.edit->value), true, error)) {
    viewportTool_.cancelDrag();
    std::string cancelError;
    if (!sceneDocument_.cancelContinuousEdit(cancelError) && error.empty())
      error = std::move(cancelError);
    if (!rebuildWorld(cancelError) && error.empty())
      error = std::move(cancelError);
    return false;
  }

  if (action.completion == EditorDragCompletion::Finish) {
    sceneDocument_.endContinuousEdit();
  } else if (action.completion == EditorDragCompletion::Cancel) {
    const std::string before = sceneDocument_.json().dump();
    if (!sceneDocument_.cancelContinuousEdit(error))
      return false;
    if (sceneDocument_.json().dump() != before && !rebuildWorld(error))
      return false;
  }
  return true;
}

EditorGizmoPresentation
EditorWorkspace::gizmoPresentation(const runtime::Vec2 viewportSize) const {
  return viewportTool_.presentation(project_->world, selectedEntityId_,
                                    sceneView_, viewportSize);
}

bool EditorWorkspace::mutateAndRebuild(
    const std::function<bool(EditorSceneDocument &, std::string &)> &mutation,
    std::string &error) {
  EditorSceneDocument before = sceneDocument_;
  if (!mutation(sceneDocument_, error)) {
    syncEditorDiagnostic();
    return false;
  }
  if (!rebuildWorld(error)) {
    sceneDocument_ = std::move(before);
    workspaceOperationError_ = error;
    syncEditorDiagnostic();
    return false;
  }
  workspaceOperationError_.clear();
  syncEditorDiagnostic();
  return true;
}

void EditorWorkspace::syncChangedEntity() {
  const std::string_view changed = sceneDocument_.lastChangedEntityId();
  const nlohmann::json *authored = sceneDocument_.entity(changed);
  if (authored == nullptr)
    return;
  auto existing = std::ranges::find(project_->world.entities, changed,
                                    &runtime::Entity::id);
  if (existing == project_->world.entities.end())
    return;
  runtime::Entity reparsed =
      runtime::scene_loading::parseSceneEntity(*authored);
  reparsed.sceneOwner = existing->sceneOwner;
  reparsed.prefabInstance = existing->prefabInstance;
  reparsed.prefabLocalId = existing->prefabLocalId;
  *existing = std::move(reparsed);
}

bool EditorWorkspace::rebuildWorld(std::string &error) {
  auto world =
      runtime::loadSceneDocument(project_->project, project_->project.mainScene,
                                 sceneDocument_.json(), error);
  if (!world)
    return false;
  project_->world = std::move(*world);
  return true;
}

void EditorWorkspace::refreshDiagnostics() {
  const ValidationSummary summary = validatePath(projectPath_.parent_path());
  diagnostics_ = summary.diagnostics;
  syncEditorDiagnostic();
}

void EditorWorkspace::syncEditorDiagnostic() {
  std::erase_if(diagnostics_, [](const Diagnostic &diagnostic) {
    return diagnostic.code == "EDITOR_SCENE_EDIT_REJECTED" ||
           diagnostic.code == "EDITOR_PREVIEW_REBUILD_FAILED";
  });
  if (const auto &issue = sceneDocument_.issue(); issue.has_value()) {
    diagnostics_.push_back({.severity = Severity::Error,
                            .code = "EDITOR_SCENE_EDIT_REJECTED",
                            .message = issue->message,
                            .path = sceneDocument_.path().string()});
  }
  if (!workspaceOperationError_.empty()) {
    diagnostics_.push_back({.severity = Severity::Error,
                            .code = "EDITOR_PREVIEW_REBUILD_FAILED",
                            .message = workspaceOperationError_,
                            .path = sceneDocument_.path().string()});
  }
}

const runtime::Entity *EditorWorkspace::selectedEntity() const {
  if (!project_)
    return nullptr;
  const auto found = std::ranges::find(project_->world.entities,
                                       selectedEntityId_, &runtime::Entity::id);
  return found == project_->world.entities.end() ? nullptr : &*found;
}

} // namespace demi::editor
