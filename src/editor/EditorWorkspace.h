#pragma once

#include "editor/EditorSourceIndex.h"

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
#include "editor/EditorTerrainAuthoring.h"
#include "editor/EditorTerrainAssetDocument.h"

#include "demi/assets/AssetImporter.h"
#include "demi/assets/ColliderAssetGenerator.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/RuntimePrefabService.h"
#include "demi/runtime/tilemap/TilemapAsset.h"

#include <filesystem>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace demi::editor {

struct EditorModule;

enum class EditorWorkspaceDocument { Scene, Hud, TerrainAsset };

struct EditorTerrainAuthoringTarget {
  std::filesystem::path document;
  std::string entityId;
};

class EditorWorkspace {
public:
  [[nodiscard]] bool open(std::filesystem::path projectPath,
                          std::string &error);
  // Initializes project services without loading the main scene or preparing
  // all terrain assets. Document sessions load only their selected source.
  [[nodiscard]] bool openProjectContext(const EditorWorkspace &source,
                                       std::string &error);
  [[nodiscard]] bool openSceneDocument(const std::filesystem::path &path,
                                       std::string &error);
  [[nodiscard]] bool openPrefabDocument(const std::filesystem::path &path,
                                        std::string &error);
  [[nodiscard]] bool
  replaceHierarchyWithPrefab(std::string_view selectedId,
                             const std::filesystem::path &prefabPath,
                             std::string &error);
  [[nodiscard]] bool instantiatePrefab(const std::filesystem::path &path,
                                        std::string &error);
  [[nodiscard]] bool instantiatePrefab(const std::filesystem::path &path,
                                       runtime::Vec2 worldPosition,
                                       std::string &error);
  [[nodiscard]] bool instantiatePrefab(const std::filesystem::path &path,
                                       runtime::Vec3 worldPosition,
                                       std::string &error);
  [[nodiscard]] bool removePrefabInstance(std::string_view expandedEntityId,
                                          std::string &error);
  [[nodiscard]] bool duplicatePrefabInstance(std::string_view expandedEntityId,
                                             std::string &error);
  [[nodiscard]] bool isPrefabDocument() const { return editingPrefab_; }
  [[nodiscard]] const std::filesystem::path &lastScenePath() const {
    return lastScenePath_;
  }
  [[nodiscard]] const std::filesystem::path &lastPrefabPath() const {
    return lastPrefabPath_;
  }
  [[nodiscard]] bool openHudDocument(const std::filesystem::path &path,
                                     std::string &error);
  [[nodiscard]] bool openTerrainAssetDocument(const std::filesystem::path &path,
                                              std::string &error);
  [[nodiscard]] bool placeTerrainAsset(const std::filesystem::path &path,
                                      std::optional<runtime::Vec3> position,
                                      std::string &error);
  [[nodiscard]] bool assignTerrainAsset(std::string_view entityId,
                                        std::string_view assetId,
                                        std::string &error);
  [[nodiscard]] bool terrainEditingAsset() const;
  [[nodiscard]] bool applyTerrainAssetChanges(std::string &error);
  void requestTerrainAssetAssign(std::string entityId, std::string assetId) {
    terrainAssetAssignRequest_ =
        std::pair{std::move(entityId), std::move(assetId)};
  }
  [[nodiscard]] std::optional<std::pair<std::string, std::string>>
  takeTerrainAssetAssignRequest() {
    return std::exchange(terrainAssetAssignRequest_, std::nullopt);
  }
  void requestTerrainAssetOpen(std::filesystem::path path) {
    terrainAssetOpenRequest_ = std::move(path);
  }
  [[nodiscard]] std::optional<std::filesystem::path>
  takeTerrainAssetOpenRequest() {
    return std::exchange(terrainAssetOpenRequest_, std::nullopt);
  }
  void requestTerrainAssetCreate(std::string selectedEntity = {}) {
    terrainAssetCreateRequest_ = std::move(selectedEntity);
  }
  [[nodiscard]] std::optional<std::string> takeTerrainAssetCreateRequest() {
    return std::exchange(terrainAssetCreateRequest_, std::nullopt);
  }
  [[nodiscard]] const EditorTerrainAssetDocument *terrainAssetDocument() const {
    return terrainAssetDocument_ ? &*terrainAssetDocument_ : nullptr;
  }
  [[nodiscard]] const std::filesystem::path &lastTerrainAssetPath() const {
    return lastTerrainAssetPath_;
  }
  [[nodiscard]] bool refresh(std::string &error);
  void pollScriptSources();
  void notifyScriptCreated(const std::filesystem::path &path) {
    sourceIndex_->changed(path);
  }
  const EditorLuaComponentCatalog &scriptCatalog() const {
    return sourceIndex_->scripts();
  }
  [[nodiscard]] bool createFolder(const std::filesystem::path &relativeParent,
                                  std::string_view name, std::string &error);
  [[nodiscard]] const std::set<std::filesystem::path> &
  sourceDirectories() const {
    return sourceIndex_->directories();
  }
  [[nodiscard]] bool save(std::string &error);
  [[nodiscard]] bool saveProject(std::string &error);
  // Adopt saved project registrations/settings without rebuilding any world.
  // Unsaved project-setting edits are never overwritten by another session.
  [[nodiscard]] bool refreshCleanProjectDocument(
      const std::filesystem::path &path, std::string &error);
  [[nodiscard]] bool setSceneHud(const std::filesystem::path &path, std::string &error);
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
  [[nodiscard]] bool setProjectMainScene(std::string_view id, std::string &error) {
    return projectDocument_.setMainScene(id, error);
  }
  [[nodiscard]] bool setProjectInputActions(nlohmann::json actions,
                                            std::string &error) {
    return projectDocument_.setInputActions(std::move(actions), error);
  }
  [[nodiscard]] bool setProjectInputPresets(std::vector<std::string> presets,
                                            std::string &error) {
    return projectDocument_.setInputPresets(std::move(presets), error);
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
  [[nodiscard]] std::optional<assets::ColliderRecommendation>
  recommendCollider(const std::filesystem::path &modelManifest,
                    std::string_view body, std::string &error);
  [[nodiscard]] std::optional<std::filesystem::path> generateColliderAsset(
      assets::ColliderAssetGenerationRequest request,
      std::optional<std::string> &existingManifestHash, std::string &error);
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
  [[nodiscard]] SceneValueTarget authoredTarget(SceneValueTarget target) const;
  [[nodiscard]] bool hasExplicitValue(SceneValueTarget target) const;
  [[nodiscard]] bool createEntity(std::string &error,
                                  std::optional<std::string> parent = {},
                                  EditorEntityKind kind = EditorEntityKind::Empty,
                                  std::optional<runtime::Vec3> worldPosition = {});
  [[nodiscard]] bool createPresetEntity(std::string_view preset,
                                        std::string &error);
  [[nodiscard]] bool unpackPreset(std::string_view id, std::string &error);
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
  [[nodiscard]] bool addComponent(std::string_view id,
                                  std::string_view componentName,
                                  nlohmann::json initialValues,
                                  std::string &error);
  [[nodiscard]] bool
  addScriptComponent(std::string_view id,
                     const EditorLuaComponentMetadata &metadata,
                     std::string &error);
  [[nodiscard]] bool removeComponent(std::string_view id,
                                     std::string_view componentName,
                                     std::string &error);
  [[nodiscard]] bool revertComponentOverride(std::string_view id,
                                             std::string_view componentName,
                                             std::string &error);
  [[nodiscard]] std::optional<std::filesystem::path>
  prefabSourcePath(std::string_view id) const;
  [[nodiscard]] std::vector<std::string>
  removedComponentOverrides(std::string_view id) const;
  [[nodiscard]] bool moveSelectedIsoGridCell(int x, int y, std::string &error);
  [[nodiscard]] bool setSelectedIsoGridCellTexture(std::string texture,
                                                   std::string &error);
  [[nodiscard]] bool deleteSelectedIsoGridCell(std::string &error);
  [[nodiscard]] bool createHudNode(std::string_view type, std::string &error);
  [[nodiscard]] bool createHudPrefabInstance(std::string_view reference, std::string &error);
  [[nodiscard]] bool placeHudModule(const EditorModule &module,
                                    runtime::Vec2 authoredPoint,
                                    std::string_view targetId,
                                    std::string &error);
  [[nodiscard]] bool setHudNodeAnchors(std::string_view id, runtime::Vec2 minimum,
                                      runtime::Vec2 maximum, std::string &error);
  [[nodiscard]] bool setHudCanvasSize(runtime::Vec2 size, std::string &error);
  [[nodiscard]] bool reparentHudNode(std::string_view id, std::string_view parent,
                                     std::string &error);
  [[nodiscard]] bool duplicateHudNode(std::string_view id, std::string &error);
  [[nodiscard]] bool deleteSelectedHudNode(std::string &error);
  [[nodiscard]] bool setHudNodeField(std::string_view id,
                                     std::string_view field,
                                     nlohmann::json value, std::string &error,
                                     bool continuous = false);
  void endHudContinuousEdit();
  [[nodiscard]] bool saveHud(std::string &error);
  // Refresh a clean linked/open HUD after another document session saved its
  // source. This does not rebuild the scene or alter scene command history.
  [[nodiscard]] bool refreshCleanHudDocument(const std::filesystem::path &path,
                                           std::string &error);
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
  [[nodiscard]] EditorTerrainAuthoring &terrainAuthoring() {
    return *terrainAuthoring_;
  }
  [[nodiscard]] const EditorTerrainAuthoring &terrainAuthoring() const {
    return *terrainAuthoring_;
  }
  void syncTerrainAuthoring();
  // A graph owns its stable source target independently of Inspector selection.
  // Unpinned authoring continues following the selected terrain as before.
  // Retargeting/unpinning require a generated or discarded draft and no
  // pending generation/stroke; hiding the panel need not release its target.
  [[nodiscard]] bool pinTerrainAuthoring(std::string_view entityId,
                                         std::string &error);
  [[nodiscard]] bool unpinTerrainAuthoring(std::string &error);
  [[nodiscard]] bool terrainAuthoringPinned() const {
    return terrainAuthoringTarget_.has_value();
  }
  [[nodiscard]] bool pollTerrainAuthoring(std::string &error);
  [[nodiscard]] bool terrainReady(std::string &error) const;
  [[nodiscard]] std::filesystem::path terrainAuthoringDocumentPath() const;
  void requestTerrainGraphOpen() noexcept { terrainGraphOpenRequested_ = true; }
  [[nodiscard]] bool takeTerrainGraphOpenRequest() noexcept {
    const bool requested = terrainGraphOpenRequested_;
    terrainGraphOpenRequested_ = false;
    return requested;
  }
  // Rescans the project's asset registry for landscape presets and repopulates
  // the authoring object's transient preset cache. Call whenever the project or
  // its asset metadata changes; the cache is editor state, never authored data.
  void refreshTerrainPresets();
  // Applies a preset to the bound terrain's draft and commits it through the
  // same undoable recipe command Generate uses, so strokes survive and Undo
  // restores the exact previous document. A preset that changes the grid while
  // strokes exist is routed through the existing resize decision instead of
  // silently invalidating protection snapshots. `error` receives the engine's
  // own rejection message verbatim.
  [[nodiscard]] bool applyTerrainPreset(std::string_view presetId,
                                        std::string &error);
  // Removes both preset provenance keys through the same undoable command.
  [[nodiscard]] bool clearTerrainPreset(std::string &error);
  [[nodiscard]] const std::vector<std::string> &terrainPresetErrors() const {
    return terrainPresetErrors_;
  }
  [[nodiscard]] bool cancelTerrainEditing(std::string &error) {
    return restoreTerrainPreview(error);
  }
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
    return sourceIndex_->sources();
  }
  [[nodiscard]] std::uint64_t sourceIndexRevision() const noexcept {
    return sourceIndex_->revision();
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
  [[nodiscard]] bool activateSceneDocument(std::string &error);
  void activateHudDocument();
  [[nodiscard]] bool activeDocumentDirty() const {
    if (terrainEditingAsset())
      return terrainAssetDocument_ &&
             (terrainAssetDocument_->isDirty() || terrainAssetCachePending_ ||
              (activeDocument_ == EditorWorkspaceDocument::Scene && sceneDocument_.isDirty()));
    const EditorHudDocument *hud = hudDocument();
    return activeDocument_ == EditorWorkspaceDocument::Hud && hud
               ? hud->isDirty()
               : sceneDocument_.isDirty();
  }
  [[nodiscard]] bool activeDocumentCanUndo() const {
    if (terrainEditingAsset() &&
        (activeDocument_ == EditorWorkspaceDocument::TerrainAsset || lastEditTerrainAsset_))
      return terrainAssetDocument_ && terrainAssetDocument_->canUndo();
    const EditorHudDocument *hud = hudDocument();
    return activeDocument_ == EditorWorkspaceDocument::Hud && hud
               ? hud->canUndo()
               : sceneDocument_.canUndo();
  }
  [[nodiscard]] bool activeDocumentCanRedo() const {
    if (terrainEditingAsset() &&
        (activeDocument_ == EditorWorkspaceDocument::TerrainAsset || lastEditTerrainAsset_))
      return terrainAssetDocument_ && terrainAssetDocument_->canRedo();
    const EditorHudDocument *hud = hudDocument();
    return activeDocument_ == EditorWorkspaceDocument::Hud && hud
               ? hud->canRedo()
               : sceneDocument_.canRedo();
  }
  [[nodiscard]] bool hasUnsavedChanges() const {
    return sceneDocument_.isDirty() || projectDocument_.isDirty() ||
           (hudDocument_ && hudDocument_->isDirty()) ||
           (openedHudDocument_ && openedHudDocument_->isDirty()) ||
           (terrainAssetDocument_ && terrainAssetDocument_->isDirty()) ||
           terrainAssetCachePending_;
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

  [[nodiscard]] bool canAlignSelectedCameraToView() const;
  [[nodiscard]] bool alignSelectedCameraToView(std::string &error);
  // Explicit selection events include selecting the same object again, so
  // presentation can reveal a collapsed hierarchy without changing documents.
  [[nodiscard]] std::uint64_t selectionRevision() const {
    return selectionRevision_;
  }
  void selectEntity(std::string id);
  void selectHudNode(std::string id);
  void toggleHudNodeSelection(std::string id);
  [[nodiscard]] bool isHudNodeSelected(std::string_view id) const;
  [[nodiscard]] const std::vector<std::string> &selectedHudNodeIds() const {
    return selectedHudNodeIds_;
  }
  [[nodiscard]] std::optional<std::string> exportSelection(std::string &error) const;
  [[nodiscard]] bool pasteSelection(std::string_view text, std::string &error);
  [[nodiscard]] bool duplicateSelection(std::string &error);
  [[nodiscard]] bool deleteSelection(std::string &error);
  [[nodiscard]] bool selectAllAuthored(std::string &error);
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
  void clearHudSelection() {
    selectedHudNodeId_.clear();
    selectedHudNodeIds_.clear();
  }
  [[nodiscard]] bool openEntityDocument(const std::filesystem::path &path,
                                        bool prefab, std::string &error);
  [[nodiscard]] std::optional<runtime::World>
  loadEntityPreview(const EditorSceneDocument &document, bool prefab,
                    std::string &error,
                    runtime::RuntimePrefabService *prefabs = nullptr) const;
  bool editingPrefab_ = false;
  std::filesystem::path lastScenePath_;
  std::filesystem::path lastPrefabPath_;
  void discoverSources();
  void refreshAssetIndex();
  void configureTerrainInputResolver();
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
  [[nodiscard]] bool restoreTerrainPreview(std::string &error);
  [[nodiscard]] bool pollTerrainAssetAuthoring(EditorTerrainCommit commit,
                                               std::string &error);
  [[nodiscard]] bool terrainAssetHistory(bool forward, std::string &error);
  [[nodiscard]] bool refreshTerrainAssetPreview(
      const nlohmann::json &recipe, std::string &error);
  [[nodiscard]] bool saveTerrainAsset(std::string &error);
  [[nodiscard]] bool bindTerrainAsset(std::string_view assetId,
                                      std::string &error);
  [[nodiscard]] bool applyTerrainAssetPreview(runtime::World &world,
                                              std::string &error,
                                              runtime::RuntimePrefabService *prefabs = nullptr);
  [[nodiscard]] bool rebuildParkedSceneWorld(std::string &error);
  void leaveTerrainAssetDocument();
  [[nodiscard]] bool terrainHistory(bool forward, std::string &error);
  [[nodiscard]] std::optional<nlohmann::json>
  effectiveTerrainRecipe(const SceneValueTarget &target, std::string &error) const;
  [[nodiscard]] bool applyViewportAction(EditorViewportToolAction action,
                                         const std::function<void()> &cancel,
                                         std::string &error);
  void updateSceneDomain(bool openingProject);
  [[nodiscard]] SceneValueTarget
  resolveSceneTarget(SceneValueTarget target) const;

  std::filesystem::path projectPath_;
  std::optional<runtime::LoadedProject> project_;
  EditorSceneDocument sceneDocument_;
  EditorProjectDocument projectDocument_;
  std::optional<EditorHudDocument> hudDocument_;
  std::optional<EditorHudDocument> openedHudDocument_;
  std::optional<EditorTerrainAssetDocument> terrainAssetDocument_;
  EditorTerrainSurfacePtr terrainAssetSurface_;
  std::optional<nlohmann::json> terrainAssetDraft_;
  bool terrainAssetBinding_ = false;
  bool lastEditTerrainAsset_ = false;
  runtime::RuntimePrefabService terrainPreviewPrefabs_;
  std::optional<runtime::RuntimePrefabService> parkedTerrainPrefabs_;
  bool terrainAssetCachePending_ = false;
  std::optional<runtime::World> parkedSceneWorld_;
  EditorSceneViewState parkedSceneView_;
  EditorSceneViewDimension parkedSceneViewDimension_ =
      EditorSceneViewDimension::ThreeDimensional;
  std::vector<std::string> parkedSceneSelection_;
  std::filesystem::path lastTerrainAssetPath_;
  EditorWorkspaceDocument activeDocument_ = EditorWorkspaceDocument::Scene;
  bool usesOpenedHudDocument_ = false;
  EditorAssetIndex assetIndex_;
  EditorSceneViewState sceneView_;
  EditorSceneView2DState sceneView2D_;
  EditorViewportTool viewportTool_;
  EditorViewportTool2D viewportTool2D_;
  std::unique_ptr<EditorTerrainAuthoring> terrainAuthoring_ =
      std::make_unique<EditorTerrainAuthoring>();
  std::optional<EditorTerrainAuthoringTarget> terrainAuthoringTarget_;
  std::optional<EditorTerrainCommit> terrainPreview_;
  EditorTerrainSurfacePtr terrainPreviewSurface_;
  EditorSceneDomain sceneDomain_ = EditorSceneDomain::Empty;
  EditorSceneViewDimension viewDimension_ =
      EditorSceneViewDimension::ThreeDimensional;
  std::shared_ptr<EditorSourceIndex> sourceIndex_ =
      std::make_shared<EditorSourceIndex>();
  std::uint64_t scriptDiagnosticsRevision_ = 0;
  std::unordered_map<std::string, runtime::TilemapAsset2D> tilemaps2D_;
  Diagnostics diagnostics_;
  std::uint64_t selectionRevision_ = 0;
  std::vector<std::string> selectedEntityIds_;
  std::string selectedHudNodeId_;
  std::vector<std::string> selectedHudNodeIds_;
  std::optional<EditorIsoGridCell> selectedIsoGridCell_;
  std::string workspaceOperationError_;
  // Loader messages for terrain preset assets that exist but cannot be read.
  // Kept next to the preset cache so the picker can explain a missing entry.
  std::vector<std::string> terrainPresetErrors_;
  bool terrainGraphOpenRequested_ = false;
  std::optional<std::string> terrainAssetCreateRequest_;
  std::optional<std::filesystem::path> terrainAssetOpenRequest_;
  std::optional<std::pair<std::string, std::string>>
      terrainAssetAssignRequest_;
};

} // namespace demi::editor
