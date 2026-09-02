#pragma once

#include "editor/EditorAssetIndex.h"
#include "editor/EditorHudDocument.h"
#include "editor/EditorLuaComponentMetadata.h"
#include "editor/EditorProjectDocument.h"
#include "editor/EditorRecoveryStore.h"
#include "editor/EditorSceneDocument.h"
#include "editor/EditorSceneDomain.h"
#include "editor/EditorSceneView2DState.h"
#include "editor/EditorSceneViewState.h"
#include "editor/EditorSelection.h"
#include "editor/EditorViewportTool.h"
#include "editor/EditorViewportTool2D.h"

#include "demi/assets/AssetImporter.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/tilemap/TilemapAsset.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace demi::editor {

enum class EditorWorkspaceDocument { Scene, Hud };

class EditorWorkspace {
public:
  [[nodiscard]] bool open(std::filesystem::path projectPath,
                          std::string &error);
  [[nodiscard]] bool openSceneDocument(const std::filesystem::path &path,
                                       std::string &error);
  [[nodiscard]] bool openHudDocument(const std::filesystem::path &path,
                                     std::string &error);
  [[nodiscard]] bool refresh(std::string &error);
  [[nodiscard]] bool save(std::string &error);
  [[nodiscard]] bool saveProject(std::string &error);
  [[nodiscard]] bool saveAll(std::string &error);
  [[nodiscard]] std::vector<EditorRecoveryDocument> dirtyDocuments() const;
  [[nodiscard]] bool applyRecovery(const EditorRecoverySnapshot &snapshot,
                                   std::string &error);
  [[nodiscard]] bool projectUndo(std::string &error);
  [[nodiscard]] bool projectRedo(std::string &error);
  [[nodiscard]] bool setPreloadedAssets(std::vector<std::string> assets,
                                        std::string &error);
  [[nodiscard]] bool addProjectScene(std::string id, std::filesystem::path path,
                                     std::string &error);
  [[nodiscard]] bool removeProjectScene(std::string_view id,
                                        std::string &error);
  [[nodiscard]] bool setProjectInputActions(nlohmann::json actions,
                                            std::string &error) {
    return projectDocument_.setInputActions(std::move(actions), error);
  }
  [[nodiscard]] bool setProjectInputBinding(std::string_view action,
                                            std::size_t bindingIndex,
                                            std::string input,
                                            std::string &error) {
    return projectDocument_.setInputBinding(action, bindingIndex,
                                            std::move(input), error);
  }
  [[nodiscard]] bool
  setProjectBuildSettings(runtime::ProjectBuildSettings settings,
                          std::string &error);
  [[nodiscard]] bool importAsset(const assets::AssetImportRequest &request,
                                 std::string &error);
  [[nodiscard]] bool reimportAsset(const std::filesystem::path &manifest,
                                   std::string &error);
  [[nodiscard]] bool createAssetGroup(std::string id,
                                      std::vector<std::string> roots,
                                      std::string &error);
  [[nodiscard]] bool
  resolveExternalChange(ExternalChangeDecision decision,
                        const std::filesystem::path &copyPath,
                        std::string &error);
  [[nodiscard]] bool undo(std::string &error);
  [[nodiscard]] bool redo(std::string &error);
  [[nodiscard]] bool editValue(SceneValueTarget target, nlohmann::json value,
                               bool continuous, std::string &error);
  [[nodiscard]] bool editValues(std::vector<SceneValueTarget> targets,
                                nlohmann::json value, std::string &error);
  [[nodiscard]] bool removeValue(SceneValueTarget target, std::string &error);
  [[nodiscard]] bool createEntity(std::string &error,
                                  std::optional<std::string> parent = {});
  [[nodiscard]] bool deleteEntity(std::string_view id, std::string &error);
  [[nodiscard]] bool deleteEntities(std::vector<std::string> ids,
                                    std::string &error);
  [[nodiscard]] bool reparentEntity(std::string_view id,
                                    std::optional<std::string> newParent,
                                    std::string &error);
  [[nodiscard]] bool duplicateEntity(std::string_view id, std::string &error);
  [[nodiscard]] bool addComponent(std::string_view id,
                                  std::string_view componentName,
                                  std::string &error);
  [[nodiscard]] bool
  addScriptComponent(std::string_view id,
                     const EditorLuaComponentMetadata &metadata,
                     std::string &error);
  [[nodiscard]] bool removeComponent(std::string_view id,
                                     std::string_view componentName,
                                     std::string &error);
  [[nodiscard]] bool moveSelectedIsoGridCell(int x, int y, std::string &error);
  [[nodiscard]] bool setSelectedIsoGridCellTexture(std::string texture,
                                                   std::string &error);
  [[nodiscard]] bool deleteSelectedIsoGridCell(std::string &error);
  [[nodiscard]] bool createHudNode(std::string_view type, std::string &error);
  [[nodiscard]] bool deleteSelectedHudNode(std::string &error);
  [[nodiscard]] bool setHudNodeField(std::string_view id,
                                     std::string_view field,
                                     nlohmann::json value, std::string &error);
  [[nodiscard]] bool saveHud(std::string &error);
  [[nodiscard]] bool updateViewportTool(const EditorViewportToolInput &input,
                                        std::string &error);
  [[nodiscard]] bool updateViewportTool2D(const EditorViewportToolInput &input,
                                          std::string &error);
  [[nodiscard]] EditorGizmoPresentation
  gizmoPresentation(runtime::Vec2 viewportSize) const;
  [[nodiscard]] EditorGizmoPresentation
  gizmoPresentation2D(runtime::Vec2 viewportSize) const;
  [[nodiscard]] EditorViewportTool &viewportTool() { return viewportTool_; }
  [[nodiscard]] const EditorViewportTool &viewportTool() const {
    return viewportTool_;
  }
  [[nodiscard]] EditorViewportTool2D &viewportTool2D() {
    return viewportTool2D_;
  }
  [[nodiscard]] const EditorViewportTool2D &viewportTool2D() const {
    return viewportTool2D_;
  }
  void endContinuousEdit() { sceneDocument_.endContinuousEdit(); }
  void refreshDiagnostics();
  void refreshAssetMetadata();

  [[nodiscard]] const runtime::LoadedProject &project() const {
    return *project_;
  }
  [[nodiscard]] runtime::LoadedProject &project() { return *project_; }
  [[nodiscard]] const std::filesystem::path &projectPath() const {
    return projectPath_;
  }
  [[nodiscard]] const std::vector<std::filesystem::path> &sources() const {
    return sources_;
  }
  [[nodiscard]] std::optional<std::filesystem::path> authoredHudPath() const;
  [[nodiscard]] const EditorHudDocument *hudDocument() const;
  [[nodiscard]] const runtime::ui::UiDocument &displayedHud() const;
  [[nodiscard]] bool hasHudDocument() const {
    return hudDocument_.has_value() || openedHudDocument_.has_value();
  }
  [[nodiscard]] EditorWorkspaceDocument activeDocument() const {
    return activeDocument_;
  }
  void activateSceneDocument();
  void activateHudDocument();
  [[nodiscard]] bool activeDocumentDirty() const {
    const EditorHudDocument *hud = hudDocument();
    return activeDocument_ == EditorWorkspaceDocument::Hud && hud
               ? hud->isDirty()
               : sceneDocument_.isDirty();
  }
  [[nodiscard]] bool activeDocumentCanUndo() const {
    const EditorHudDocument *hud = hudDocument();
    return activeDocument_ == EditorWorkspaceDocument::Hud && hud
               ? hud->canUndo()
               : sceneDocument_.canUndo();
  }
  [[nodiscard]] bool activeDocumentCanRedo() const {
    const EditorHudDocument *hud = hudDocument();
    return activeDocument_ == EditorWorkspaceDocument::Hud && hud
               ? hud->canRedo()
               : sceneDocument_.canRedo();
  }
  [[nodiscard]] bool hasUnsavedChanges() const {
    return sceneDocument_.isDirty() || projectDocument_.isDirty() ||
           (hudDocument_ && hudDocument_->isDirty()) ||
           (openedHudDocument_ && openedHudDocument_->isDirty());
  }
  [[nodiscard]] bool hudDirty() const {
    const EditorHudDocument *hud = hudDocument();
    return hud && hud->isDirty();
  }
  [[nodiscard]] const auto &tilemaps2D() const { return tilemaps2D_; }
  [[nodiscard]] const Diagnostics &diagnostics() const { return diagnostics_; }
  [[nodiscard]] const EditorAssetIndex &assetIndex() const {
    return assetIndex_;
  }
  [[nodiscard]] const EditorProjectDocument &projectDocument() const {
    return projectDocument_;
  }
  [[nodiscard]] const EditorSceneDocument &sceneDocument() const {
    return sceneDocument_;
  }
  [[nodiscard]] EditorSceneDocument &sceneDocument() { return sceneDocument_; }
  [[nodiscard]] const EditorSceneViewState &sceneView() const {
    return sceneView_;
  }
  [[nodiscard]] EditorSceneViewState &sceneView() { return sceneView_; }
  [[nodiscard]] const EditorSceneView2DState &sceneView2D() const {
    return sceneView2D_;
  }
  [[nodiscard]] EditorSceneView2DState &sceneView2D() { return sceneView2D_; }
  [[nodiscard]] EditorSceneDomain sceneDomain() const { return sceneDomain_; }
  [[nodiscard]] EditorSceneViewDimension viewDimension() const {
    return viewDimension_;
  }
  void setViewDimension(EditorSceneViewDimension dimension);

  void selectEntity(std::string id);
  void selectHudNode(std::string id);
  void selectIsoGridCell(EditorIsoGridCell cell);
  void toggleEntitySelection(std::string id);
  [[nodiscard]] bool isEntitySelected(std::string_view id) const;
  [[nodiscard]] const std::vector<std::string> &selectedEntityIds() const {
    return selectedEntityIds_;
  }
  [[nodiscard]] std::string_view selectedEntityId() const {
    return selectedEntityIds_.empty() ? std::string_view{}
                                      : selectedEntityIds_.back();
  }
  [[nodiscard]] const runtime::Entity *selectedEntity() const;
  [[nodiscard]] std::string_view selectedHudNodeId() const {
    return selectedHudNodeId_;
  }
  [[nodiscard]] const runtime::ui::UiNode *selectedHudNode() const;
  [[nodiscard]] const std::optional<EditorIsoGridCell> &
  selectedIsoGridCell() const {
    return selectedIsoGridCell_;
  }

private:
  void discoverSources();
  void refreshAssetIndex();
  void loadPreviewTilemaps();
  void syncChangedEntity();
  void reconcileIsoGridCellSelection();
  void syncEditorDiagnostic();
  [[nodiscard]] bool loadHudDocument(std::string &error);
  [[nodiscard]] EditorHudDocument *activeHudDocument();
  void syncHudPreview();
  [[nodiscard]] bool mutateAndRebuild(
      const std::function<bool(EditorSceneDocument &, std::string &)> &mutation,
      std::string &error);
  [[nodiscard]] bool rebuildWorld(std::string &error);
  [[nodiscard]] bool applyViewportAction(EditorViewportToolAction action,
                                         const std::function<void()> &cancel,
                                         std::string &error);
  void updateSceneDomain(bool openingProject);

  std::filesystem::path projectPath_;
  std::optional<runtime::LoadedProject> project_;
  EditorSceneDocument sceneDocument_;
  EditorProjectDocument projectDocument_;
  std::optional<EditorHudDocument> hudDocument_;
  std::optional<EditorHudDocument> openedHudDocument_;
  EditorWorkspaceDocument activeDocument_ = EditorWorkspaceDocument::Scene;
  bool usesOpenedHudDocument_ = false;
  EditorAssetIndex assetIndex_;
  EditorSceneViewState sceneView_;
  EditorSceneView2DState sceneView2D_;
  EditorViewportTool viewportTool_;
  EditorViewportTool2D viewportTool2D_;
  EditorSceneDomain sceneDomain_ = EditorSceneDomain::Empty;
  EditorSceneViewDimension viewDimension_ =
      EditorSceneViewDimension::ThreeDimensional;
  std::vector<std::filesystem::path> sources_;
  std::unordered_map<std::string, runtime::TilemapAsset2D> tilemaps2D_;
  Diagnostics diagnostics_;
  std::vector<std::string> selectedEntityIds_;
  std::string selectedHudNodeId_;
  std::optional<EditorIsoGridCell> selectedIsoGridCell_;
  std::string workspaceOperationError_;
};

} // namespace demi::editor
