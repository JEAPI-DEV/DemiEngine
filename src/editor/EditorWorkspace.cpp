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
  if (!sceneDocument_.reload(error))
    return false;
  auto loaded = runtime::loadProject(projectPath_, error);
  if (!loaded)
    return false;
  project_ = std::move(loaded);
  discoverSources();
  if (selectedEntity() == nullptr && !project_->world.entities.empty())
    selectedEntityId_ = project_->world.entities.front().id;
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::save(std::string &error) {
  if (!sceneDocument_.save(error))
    return false;
  auto loaded = runtime::loadProject(projectPath_, error);
  if (!loaded)
    return false;
  project_ = std::move(loaded);
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::undo(std::string &error) {
  if (!sceneDocument_.undo(error))
    return false;
  if (!rebuildWorld(error))
    return false;
  if (selectedEntity() == nullptr && !project_->world.entities.empty())
    selectedEntityId_ = project_->world.entities.front().id;
  return true;
}

bool EditorWorkspace::redo(std::string &error) {
  if (!sceneDocument_.redo(error))
    return false;
  if (!rebuildWorld(error))
    return false;
  if (selectedEntity() == nullptr && !project_->world.entities.empty())
    selectedEntityId_ = project_->world.entities.front().id;
  return true;
}

bool EditorWorkspace::editValue(SceneValueTarget target, nlohmann::json value,
                                const bool continuous, std::string &error) {
  if (!sceneDocument_.setValue(std::move(target), std::move(value), continuous,
                               error))
    return false;
  syncChangedEntity();
  return true;
}

bool EditorWorkspace::createEntity(std::string &error) {
  if (!sceneDocument_.createEntity(error))
    return false;
  if (!rebuildWorld(error))
    return false;
  selectEntity(std::string(sceneDocument_.lastChangedEntityId()));
  return true;
}

bool EditorWorkspace::deleteEntity(const std::string_view id,
                                   std::string &error) {
  if (!sceneDocument_.deleteEntity(id, error))
    return false;
  if (!rebuildWorld(error))
    return false;
  if (selectedEntity() == nullptr)
    selectedEntityId_.clear();
  return true;
}

bool EditorWorkspace::reparentEntity(const std::string_view id,
                                     std::optional<std::string> newParent,
                                     std::string &error) {
  if (!sceneDocument_.reparent(id, std::move(newParent), error))
    return false;
  return rebuildWorld(error);
}

bool EditorWorkspace::duplicateEntity(const std::string_view id,
                                      std::string &error) {
  if (!sceneDocument_.duplicateEntity(id, error))
    return false;
  if (!rebuildWorld(error))
    return false;
  selectEntity(std::string(sceneDocument_.lastChangedEntityId()));
  return true;
}

bool EditorWorkspace::addComponent(const std::string_view id,
                                   const std::string_view componentName,
                                   std::string &error) {
  if (!sceneDocument_.addComponent(id, componentName, error))
    return false;
  return rebuildWorld(error);
}

bool EditorWorkspace::removeComponent(const std::string_view id,
                                      const std::string_view componentName,
                                      std::string &error) {
  if (!sceneDocument_.removeComponent(id, componentName, error))
    return false;
  return rebuildWorld(error);
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
  auto world = runtime::loadSceneDocument(project_->project,
                                          project_->project.mainScene,
                                          sceneDocument_.json(), error);
  if (!world)
    return false;
  project_->world = std::move(*world);
  return true;
}

void EditorWorkspace::refreshDiagnostics() {
  const ValidationSummary summary = validatePath(projectPath_.parent_path());
  diagnostics_ = summary.diagnostics;
}

const runtime::Entity *EditorWorkspace::selectedEntity() const {
  if (!project_)
    return nullptr;
  const auto found = std::ranges::find(project_->world.entities,
                                       selectedEntityId_, &runtime::Entity::id);
  return found == project_->world.entities.end() ? nullptr : &*found;
}

} // namespace demi::editor
