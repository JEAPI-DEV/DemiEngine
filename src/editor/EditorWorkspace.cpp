#include "editor/EditorWorkspace.h"

#include "editor/EditorIsoGridCell.h"
#include "editor/EditorIsoGridCellDocument.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/SceneEntityParser.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/IsoGridComponent.h"
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
  if (!projectDocument_.open(projectPath_, error)) {
    project_.reset();
    return false;
  }
  if (!sceneDocument_.open(project_->world.scenePath, error)) {
    project_.reset();
    return false;
  }
  if (!loadHudDocument(error)) {
    project_.reset();
    return false;
  }
  sceneView_.reset(project_->world);
  sceneView2D_.reset(project_->world);
  viewportTool_.cancelDrag();
  viewportTool2D_.cancelDrag();
  updateSceneDomain(true);
  discoverSources();
  refreshAssetIndex();
  loadPreviewTilemaps();
  if (!project_->world.entities.empty())
    selectEntity(project_->world.entities.front().id);
  refreshDiagnostics();
  return true;
}

void EditorWorkspace::loadPreviewTilemaps() {
  tilemaps2D_.clear();
  const AssetRegistry registry =
      loadAssetRegistry(project_->project.projectDirectory);
  for (const AssetManifest &asset : registry.assets) {
    if (asset.type != "Tilemap2D")
      continue;
    std::string ignored;
    if (auto tilemap = runtime::loadTilemapAsset(asset, ignored))
      tilemaps2D_.insert_or_assign(asset.id, std::move(*tilemap));
  }
}

bool EditorWorkspace::refresh(std::string &error) {
  if (sceneDocument_.isDirty() || projectDocument_.isDirty() ||
      (hudDocument_ && hudDocument_->isDirty())) {
    error =
        "The scene or project has unsaved changes. Save or undo them before "
        "refreshing.";
    return false;
  }
  auto loaded = runtime::loadProject(projectPath_, error);
  if (!loaded)
    return false;
  if (!sceneDocument_.reload(error))
    return false;
  if (!projectDocument_.reload(error))
    return false;
  project_ = std::move(loaded);
  if (!loadHudDocument(error))
    return false;
  viewportTool_.cancelDrag();
  viewportTool2D_.cancelDrag();
  updateSceneDomain(false);
  discoverSources();
  refreshAssetIndex();
  loadPreviewTilemaps();
  std::erase_if(selectedEntityIds_, [this](const std::string &id) {
    return std::ranges::find(project_->world.entities, id,
                             &runtime::Entity::id) ==
           project_->world.entities.end();
  });
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::save(std::string &error) {
  if (!selectedHudNodeId_.empty() && hudDocument_ && hudDocument_->isDirty())
    return saveHud(error);
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

bool EditorWorkspace::saveAll(std::string &error) {
  if (hudDirty() && !saveHud(error))
    return false;
  if (sceneDocument_.isDirty() && !save(error))
    return false;
  if (projectDocument_.isDirty() && !saveProject(error))
    return false;
  return true;
}

std::vector<EditorRecoveryDocument> EditorWorkspace::dirtyDocuments() const {
  std::vector<EditorRecoveryDocument> documents;
  if (sceneDocument_.isDirty())
    documents.push_back({.path = sceneDocument_.path(),
                         .kind = "scene",
                         .content = sceneDocument_.json()});
  if (projectDocument_.isDirty())
    documents.push_back({.path = projectDocument_.path(),
                         .kind = "project",
                         .content = projectDocument_.json()});
  if (hudDocument_ && hudDocument_->isDirty())
    documents.push_back({.path = hudDocument_->path(),
                         .kind = "hud",
                         .content = hudDocument_->json()});
  return documents;
}

bool EditorWorkspace::applyRecovery(const EditorRecoverySnapshot &snapshot,
                                    std::string &error) {
  EditorSceneDocument sceneBefore = sceneDocument_;
  EditorProjectDocument projectBefore = projectDocument_;
  std::optional<EditorHudDocument> hudBefore = hudDocument_;
  const auto samePath = [](const std::filesystem::path &left,
                           const std::filesystem::path &right) {
    return std::filesystem::absolute(left).lexically_normal() ==
           std::filesystem::absolute(right).lexically_normal();
  };
  for (const EditorRecoveryDocument &document : snapshot.documents) {
    bool restored = false;
    if (document.kind == "scene" &&
        samePath(document.path, sceneDocument_.path()))
      restored = sceneDocument_.restore(document.content, error);
    else if (document.kind == "project" &&
             samePath(document.path, projectDocument_.path()))
      restored = projectDocument_.restore(document.content, error);
    else if (document.kind == "hud" && hudDocument_ &&
             samePath(document.path, hudDocument_->path()))
      restored = hudDocument_->restore(document.content, error);
    else
      continue;
    if (!restored) {
      sceneDocument_ = std::move(sceneBefore);
      projectDocument_ = std::move(projectBefore);
      hudDocument_ = std::move(hudBefore);
      return false;
    }
  }
  if (!rebuildWorld(error)) {
    sceneDocument_ = std::move(sceneBefore);
    projectDocument_ = std::move(projectBefore);
    hudDocument_ = std::move(hudBefore);
    return false;
  }
  syncHudPreview();
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
    viewportTool2D_.cancelDrag();
    updateSceneDomain(false);
    discoverSources();
    refreshAssetIndex();
    loadPreviewTilemaps();
    std::erase_if(selectedEntityIds_, [this](const std::string &id) {
      return std::ranges::find(project_->world.entities, id,
                               &runtime::Entity::id) ==
             project_->world.entities.end();
    });
  }
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::undo(std::string &error) {
  if (!selectedHudNodeId_.empty() && hudDocument_ && hudDocument_->canUndo()) {
    if (!hudDocument_->undo(error))
      return false;
    syncHudPreview();
    return true;
  }
  if (!mutateAndRebuild(
          [](EditorSceneDocument &document, std::string &mutationError) {
            return document.undo(mutationError);
          },
          error))
    return false;
  reconcileIsoGridCellSelection();
  if (selectedEntity() == nullptr && !project_->world.entities.empty())
    selectEntity(project_->world.entities.front().id);
  return true;
}

bool EditorWorkspace::redo(std::string &error) {
  if (!selectedHudNodeId_.empty() && hudDocument_ && hudDocument_->canRedo()) {
    if (!hudDocument_->redo(error))
      return false;
    syncHudPreview();
    return true;
  }
  if (!mutateAndRebuild(
          [](EditorSceneDocument &document, std::string &mutationError) {
            return document.redo(mutationError);
          },
          error))
    return false;
  reconcileIsoGridCellSelection();
  if (selectedEntity() == nullptr && !project_->world.entities.empty())
    selectEntity(project_->world.entities.front().id);
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

bool EditorWorkspace::editValues(std::vector<SceneValueTarget> targets,
                                 nlohmann::json value, std::string &error) {
  return mutateAndRebuild(
      [targets = std::move(targets), value = std::move(value)](
          EditorSceneDocument &document, std::string &mutationError) mutable {
        return document.setValues(std::move(targets), std::move(value),
                                  mutationError);
      },
      error);
}

bool EditorWorkspace::removeValue(SceneValueTarget target, std::string &error) {
  return mutateAndRebuild(
      [target = std::move(target)](EditorSceneDocument &document,
                                   std::string &mutationError) mutable {
        return document.removeValue(std::move(target), mutationError);
      },
      error);
}

bool EditorWorkspace::createEntity(std::string &error,
                                   std::optional<std::string> parent) {
  if (!mutateAndRebuild(
          [parent = std::move(parent)](EditorSceneDocument &document,
                                       std::string &mutationError) mutable {
            return document.createEntity(mutationError, std::move(parent));
          },
          error))
    return false;
  selectEntity(std::string(sceneDocument_.lastChangedEntityId()));
  return true;
}

bool EditorWorkspace::deleteEntity(const std::string_view id,
                                   std::string &error) {
  return deleteEntities({std::string(id)}, error);
}

bool EditorWorkspace::deleteEntities(std::vector<std::string> ids,
                                     std::string &error) {
  if (!mutateAndRebuild(
          [ids = std::move(ids)](EditorSceneDocument &document,
                                 std::string &mutationError) {
            return document.deleteEntities(ids, mutationError);
          },
          error))
    return false;
  std::erase_if(selectedEntityIds_, [this](const std::string &selected) {
    return sceneDocument_.entity(selected) == nullptr;
  });
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

bool EditorWorkspace::moveSelectedIsoGridCell(const int x, const int y,
                                              std::string &error) {
  if (!selectedIsoGridCell_) {
    error = "Select a painted grid cell first.";
    return false;
  }
  const EditorIsoGridCell before = *selectedIsoGridCell_;
  const runtime::Entity *entity =
      runtime::findEntity(project_->world, before.gridEntityId);
  const auto *grid = entity == nullptr
                         ? nullptr
                         : entity->component<runtime::IsoGridComponent>();
  if (grid == nullptr) {
    error = "The selected isometric grid no longer exists.";
    return false;
  }
  const nlohmann::json *component =
      sceneDocument_.component(before.gridEntityId, "IsoGrid");
  if (component == nullptr) {
    error = "The authored isometric grid no longer exists.";
    return false;
  }
  auto cells = moveAuthoredIsoGridCell(*component, before, x, y, grid->width,
                                       grid->height, error);
  if (!cells)
    return false;
  if (!editValue({.entityId = before.gridEntityId,
                  .component = "IsoGrid",
                  .field = "cell_textures"},
                 std::move(*cells), false, error))
    return false;
  selectedIsoGridCell_ =
      EditorIsoGridCell{.gridEntityId = before.gridEntityId, .x = x, .y = y};
  return true;
}

bool EditorWorkspace::setSelectedIsoGridCellTexture(std::string texture,
                                                    std::string &error) {
  if (!selectedIsoGridCell_) {
    error = "Select a painted grid cell first.";
    return false;
  }
  const nlohmann::json *component =
      sceneDocument_.component(selectedIsoGridCell_->gridEntityId, "IsoGrid");
  if (component == nullptr) {
    error = "The authored isometric grid no longer exists.";
    return false;
  }
  auto cells = setAuthoredIsoGridCellTexture(*component, *selectedIsoGridCell_,
                                             std::move(texture), error);
  if (!cells)
    return false;
  return editValue({.entityId = selectedIsoGridCell_->gridEntityId,
                    .component = "IsoGrid",
                    .field = "cell_textures"},
                   std::move(*cells), false, error);
}

bool EditorWorkspace::deleteSelectedIsoGridCell(std::string &error) {
  if (!selectedIsoGridCell_) {
    error = "Select a painted grid cell first.";
    return false;
  }
  const EditorIsoGridCell selected = *selectedIsoGridCell_;
  const nlohmann::json *component =
      sceneDocument_.component(selected.gridEntityId, "IsoGrid");
  if (component == nullptr) {
    error = "The authored isometric grid no longer exists.";
    return false;
  }
  auto cells = clearAuthoredIsoGridCell(*component, selected, error);
  if (!cells)
    return false;
  if (!editValue({.entityId = selected.gridEntityId,
                  .component = "IsoGrid",
                  .field = "cell_textures"},
                 std::move(*cells), false, error))
    return false;
  selectedIsoGridCell_.reset();
  return true;
}

bool EditorWorkspace::createHudNode(const std::string_view type,
                                    std::string &error) {
  if (!hudDocument_) {
    error = "The current scene has no authored HUD document.";
    return false;
  }
  std::string parent(selectedHudNodeId_);
  if (parent.empty() && !hudDocument_->preview().nodes.empty())
    parent = hudDocument_->preview().nodes.front().id;
  std::string created;
  if (!hudDocument_->createNode(type, parent, created, error))
    return false;
  syncHudPreview();
  selectHudNode(std::move(created));
  return true;
}

bool EditorWorkspace::deleteSelectedHudNode(std::string &error) {
  if (!hudDocument_ || selectedHudNodeId_.empty()) {
    error = "Select an authored HUD element first.";
    return false;
  }
  if (!hudDocument_->deleteNode(selectedHudNodeId_, error))
    return false;
  selectedHudNodeId_.clear();
  syncHudPreview();
  return true;
}

bool EditorWorkspace::setHudNodeField(const std::string_view id,
                                      const std::string_view field,
                                      nlohmann::json value,
                                      std::string &error) {
  if (!hudDocument_) {
    error = "The current scene has no authored HUD document.";
    return false;
  }
  if (!hudDocument_->setNodeField(id, field, std::move(value), error))
    return false;
  syncHudPreview();
  return true;
}

bool EditorWorkspace::saveHud(std::string &error) {
  if (!hudDocument_)
    return true;
  if (!hudDocument_->save(error))
    return false;
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::updateViewportTool(const EditorViewportToolInput &input,
                                         std::string &error) {
  return applyViewportAction(
      viewportTool_.update(project_->world, selectedEntityId(), sceneView_,
                           input),
      [this] { viewportTool_.cancelDrag(); }, error);
}

bool EditorWorkspace::updateViewportTool2D(const EditorViewportToolInput &input,
                                           std::string &error) {
  return applyViewportAction(
      viewportTool2D_.update(project_->world, selectedEntityId(), sceneView2D_,
                             input, selectedIsoGridCell_),
      [this] { viewportTool2D_.cancelDrag(); }, error);
}

bool EditorWorkspace::applyViewportAction(EditorViewportToolAction action,
                                          const std::function<void()> &cancel,
                                          std::string &error) {
  if (action.selectionChanged)
    selectEntity(std::move(action.selectedEntityId));
  if (action.isoGridCellSelectionChanged) {
    if (action.selectedIsoGridCell)
      selectIsoGridCell(std::move(*action.selectedIsoGridCell));
    else
      selectedIsoGridCell_.reset();
  }

  if (action.edit && !editValue(std::move(action.edit->target),
                                std::move(action.edit->value), true, error)) {
    cancel();
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
    reconcileIsoGridCellSelection();
  }
  return true;
}

EditorGizmoPresentation
EditorWorkspace::gizmoPresentation(const runtime::Vec2 viewportSize) const {
  return viewportTool_.presentation(project_->world, selectedEntityId(),
                                    sceneView_, viewportSize);
}

EditorGizmoPresentation
EditorWorkspace::gizmoPresentation2D(const runtime::Vec2 viewportSize) const {
  return viewportTool2D_.presentation(project_->world, selectedEntityId(),
                                      sceneView2D_, viewportSize,
                                      selectedIsoGridCell_);
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

void EditorWorkspace::reconcileIsoGridCellSelection() {
  if (!selectedIsoGridCell_)
    return;
  const runtime::Entity *entity =
      runtime::findEntity(project_->world, selectedIsoGridCell_->gridEntityId);
  const auto *grid = entity == nullptr
                         ? nullptr
                         : entity->component<runtime::IsoGridComponent>();
  if (grid == nullptr || !grid->cellTextures.contains(isoGridCellKey(
                             selectedIsoGridCell_->x, selectedIsoGridCell_->y)))
    selectedIsoGridCell_.reset();
}

bool EditorWorkspace::rebuildWorld(std::string &error) {
  error.clear();
  auto world =
      runtime::loadSceneDocument(project_->project, project_->project.mainScene,
                                 sceneDocument_.json(), error);
  if (!world)
    return false;
  project_->world = std::move(*world);
  syncHudPreview();
  updateSceneDomain(false);
  return true;
}

void EditorWorkspace::setViewDimension(
    const EditorSceneViewDimension dimension) {
  if (sceneDomain_ == EditorSceneDomain::TwoDimensional &&
      dimension != EditorSceneViewDimension::TwoDimensional)
    return;
  if (sceneDomain_ == EditorSceneDomain::ThreeDimensional &&
      dimension != EditorSceneViewDimension::ThreeDimensional)
    return;
  viewportTool_.cancelDrag();
  viewportTool2D_.cancelDrag();
  viewDimension_ = dimension;
}

void EditorWorkspace::updateSceneDomain(const bool openingProject) {
  sceneDomain_ = detectEditorSceneDomain(project_->world);
  if (openingProject || sceneDomain_ != EditorSceneDomain::Mixed)
    viewDimension_ = defaultEditorSceneViewDimension(sceneDomain_);
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
  const auto found = std::ranges::find(
      project_->world.entities, selectedEntityId(), &runtime::Entity::id);
  return found == project_->world.entities.end() ? nullptr : &*found;
}

const runtime::ui::UiNode *EditorWorkspace::selectedHudNode() const {
  if (!project_ || selectedHudNodeId_.empty())
    return nullptr;
  const auto found = std::ranges::find(
      project_->world.ui.nodes, selectedHudNodeId_, &runtime::ui::UiNode::id);
  return found == project_->world.ui.nodes.end() ? nullptr : &*found;
}

std::optional<std::filesystem::path> EditorWorkspace::authoredHudPath() const {
  const auto hud = sceneDocument_.json().find("hud");
  if (hud == sceneDocument_.json().end() || !hud->is_string() ||
      hud->get<std::string>().empty())
    return std::nullopt;
  return (sceneDocument_.path().parent_path() / hud->get<std::string>())
      .lexically_normal();
}

bool EditorWorkspace::loadHudDocument(std::string &error) {
  hudDocument_.reset();
  selectedHudNodeId_.clear();
  const auto path = authoredHudPath();
  if (!path)
    return true;
  EditorHudDocument document;
  if (!document.open(*path, error))
    return false;
  hudDocument_ = std::move(document);
  syncHudPreview();
  return true;
}

void EditorWorkspace::syncHudPreview() {
  if (!project_ || !hudDocument_)
    return;
  project_->world.ui = hudDocument_->preview();
  project_->world.hudCanvasSize = project_->world.ui.canvasSize;
  if (!selectedHudNodeId_.empty() && selectedHudNode() == nullptr)
    selectedHudNodeId_.clear();
}

void EditorWorkspace::selectEntity(std::string id) {
  selectedIsoGridCell_.reset();
  selectedHudNodeId_.clear();
  selectedEntityIds_.clear();
  if (!id.empty())
    selectedEntityIds_.push_back(std::move(id));
}

void EditorWorkspace::selectHudNode(std::string id) {
  selectedIsoGridCell_.reset();
  selectedEntityIds_.clear();
  selectedHudNodeId_ = std::move(id);
}

void EditorWorkspace::selectIsoGridCell(EditorIsoGridCell cell) {
  selectEntity(cell.gridEntityId);
  selectedIsoGridCell_ = std::move(cell);
}

void EditorWorkspace::toggleEntitySelection(std::string id) {
  selectedIsoGridCell_.reset();
  const auto found = std::ranges::find(selectedEntityIds_, id);
  if (found == selectedEntityIds_.end())
    selectedEntityIds_.push_back(std::move(id));
  else
    selectedEntityIds_.erase(found);
}

bool EditorWorkspace::isEntitySelected(const std::string_view id) const {
  return std::ranges::find(selectedEntityIds_, id) != selectedEntityIds_.end();
}

} // namespace demi::editor
