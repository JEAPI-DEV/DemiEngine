#include "editor/EditorWorkspace.h"
#include "demi/filesystem/ProjectPaths.h"

#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "editor/EditorIsoGridCell.h"
#include "editor/EditorIsoGridCellDocument.h"
#include "editor/EditorPrefabPlacement.h"
#include "editor/EditorScenePreview.h"
#include "editor/EditorTerrainPicking.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/runtime/scene/SceneEntityParser.h"
#include "demi/runtime/scene/ProjectParser.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/IsoGridComponent.h"
#include "demi/runtime/scene/composition/EntityHierarchy.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/schema/Validation.h"
#include "editor/EditorHudHierarchy.h"
#include "editor/EditorModuleCatalog.h"

#include <algorithm>
#include <exception>
#include <iterator>

namespace demi::editor {
namespace {

std::filesystem::path normalized(const std::filesystem::path &path) {
  return std::filesystem::absolute(path).lexically_normal();
}

bool samePath(const std::filesystem::path &left,
              const std::filesystem::path &right) {
  if (left.empty() || right.empty())
    return left.empty() && right.empty();
  return normalized(left) == normalized(right);
}

} // namespace

bool EditorWorkspace::openProjectContext(const EditorWorkspace &source,
                                       std::string &error) {
  if (project_) {
    error = "Project-only context initialization requires a new workspace.";
    return false;
  }
  if (!source.project_) {
    error = "Open a project before creating a document session.";
    return false;
  }
  EditorProjectDocument document;
  if (!document.open(source.projectPath_, error))
    return false;
  auto metadata = runtime::scene_loading::parseProjectData(
      source.projectPath_, document.json(), error);
  if (!metadata)
    return false;
  projectPath_ = source.projectPath_;
  project_.emplace(runtime::LoadedProject{.project = std::move(*metadata)});
  projectDocument_ = std::move(document);
  terrainPreviewPrefabs_.configure(project_->project.projectDirectory);
  sourceIndex_ = source.sourceIndex_;
  assetIndex_ = source.assetIndex_;
  tilemaps2D_ = source.tilemaps2D_;
  configureTerrainInputResolver();
  refreshTerrainPresets();
  return true;
}

bool EditorWorkspace::open(std::filesystem::path projectPath,
                           std::string &error) {
  if (project_ &&
      (hasUnsavedChanges() || terrainAuthoring_->hasDraftChanges() ||
       terrainAssetDraft_)) {
    error = "Save or undo documents and generate or discard terrain drafts "
            "before opening a project.";
    return false;
  }
  if (!restoreTerrainPreview(error))
    return false;
  terrainAuthoring_->unbind();
  terrainAuthoringTarget_.reset();
  terrainAssetDocument_.reset();
  terrainAssetSurface_.reset();
  terrainAssetDraft_.reset();
  terrainAssetBinding_ = false;
  lastEditTerrainAsset_ = false;
  terrainAssetCachePending_ = false;
  parkedSceneWorld_.reset();
  parkedTerrainPrefabs_.reset();
  parkedSceneSelection_.clear();
  lastTerrainAssetPath_.clear();
  if (std::filesystem::is_directory(projectPath))
    projectPath /= "demi.project.json";
  projectPath = std::filesystem::absolute(projectPath).lexically_normal();

  try {
    assets::prepareTerrainAssets(loadAssetRegistry(projectPath.parent_path()));
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }

  auto loaded = runtime::loadProject(projectPath, error);
  if (!loaded)
    return false;

  projectPath_ = std::move(projectPath);
  project_ = std::move(loaded);
  terrainPreviewPrefabs_.configure(project_->project.projectDirectory);
  openedHudDocument_.reset();
  editingPrefab_ = false;
  lastPrefabPath_.clear();
  lastScenePath_ = project_->world.scenePath;
  activeDocument_ = EditorWorkspaceDocument::Scene;
  usesOpenedHudDocument_ = false;
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
  if (!rebuildWorld(error)) {
    project_.reset();
    return false;
  }
  sceneView_.reset(project_->world);
  sceneView2D_.reset(project_->world);
  viewportTool_.cancelDrag();
  viewportTool2D_.cancelDrag();
  updateSceneDomain(true);
  if (sourceIndex_->root() != project_->project.projectDirectory)
    sourceIndex_ = std::make_shared<EditorSourceIndex>();
  discoverSources();
  refreshAssetIndex();
  refreshTerrainPresets();
  loadPreviewTilemaps();
  if (!project_->world.entities.empty())
    selectEntity(project_->world.entities.front().id);
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::openSceneDocument(const std::filesystem::path &path,
                                        std::string &error) {
  return openEntityDocument(path, false, error);
}

bool EditorWorkspace::openEntityDocument(const std::filesystem::path &path,
                                         bool prefab, std::string &error) {
  if (!project_) {
    error = "Open a project before opening a scene.";
    return false;
  }
  if (samePath(path, sceneDocument_.path())) {
    return activateSceneDocument(error);
  }
  if (terrainAuthoring_->hasDraftChanges()) {
    error = "Generate or discard the terrain graph draft before switching "
            "entity documents.";
    return false;
  }
  if (sceneDocument_.isDirty() || (hudDocument_ && hudDocument_->isDirty())) {
    error =
        "Save or undo the active scene and its HUD before switching scenes.";
    return false;
  }
  if (!restoreTerrainPreview(error))
    return false;
  if (terrainAssetDocument_) {
    if (terrainAssetDocument_->isDirty() || terrainAssetCachePending_) {
      error = "Save or undo the Terrain asset before switching scenes.";
      return false;
    }
    leaveTerrainAssetDocument();
  }
  terrainAuthoring_->unbind();

  const auto entry = std::ranges::find_if(
      project_->project.scenes, [&](const runtime::SceneEntry &candidate) {
        return samePath(project_->project.projectDirectory / candidate.path,
                        path);
      });
  if (!prefab && entry == project_->project.scenes.end()) {
    error =
        "This scene is not registered in demi.project.json: " + path.string();
    return false;
  }

  const std::filesystem::path scenePath =
      prefab ? path : project_->project.projectDirectory / entry->path;
  EditorSceneDocument scene;
  if (!scene.open(scenePath, error))
    return false;
  runtime::RuntimePrefabService previewPrefabs;
  previewPrefabs.configure(project_->project.projectDirectory);
  auto world = loadEntityPreview(scene, prefab, error, &previewPrefabs);
  if (!world)
    return false;

  std::optional<EditorHudDocument> hud;
  const auto authoredHud = scene.json().find("hud");
  if (authoredHud != scene.json().end() && authoredHud->is_string() &&
      !authoredHud->get_ref<const std::string &>().empty()) {
    EditorHudDocument candidate;
    if (!candidate.open(scene.path().parent_path() /
                            authoredHud->get_ref<const std::string &>(),
                        error))
      return false;
    hud = std::move(candidate);
  }

  sceneDocument_ = std::move(scene);
  editingPrefab_ = prefab;
  if (prefab)
    lastPrefabPath_ = sceneDocument_.path();
  else
    lastScenePath_ = sceneDocument_.path();
  hudDocument_ = std::move(hud);
  project_->world = std::move(*world);
  terrainPreviewPrefabs_ = std::move(previewPrefabs);
  activeDocument_ = EditorWorkspaceDocument::Scene;
  usesOpenedHudDocument_ = false;
  clearHudSelection();
  selectedEntityIds_.clear();
  selectedIsoGridCell_.reset();
  viewportTool_.cancelDrag();
  viewportTool2D_.cancelDrag();
  sceneView_.reset(project_->world);
  sceneView2D_.reset(project_->world);
  updateSceneDomain(true);
  if (!project_->world.entities.empty())
    selectEntity(project_->world.entities.front().id);
  if (prefab && !selectedEntityId().empty())
    (void)sceneView_.frameEntity(project_->world, selectedEntityId());
  sceneView_.studioLighting = prefab;
  workspaceOperationError_.clear();
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::openHudDocument(const std::filesystem::path &path,
                                      std::string &error) {
  if (terrainAssetDocument_) {
    if (terrainAssetDocument_->isDirty() || terrainAssetCachePending_) {
      error = "Save or undo the Terrain asset before switching documents.";
      return false;
    }
    leaveTerrainAssetDocument();
  }
  if (hudDocument_ && samePath(path, hudDocument_->path())) {
    if (openedHudDocument_ && openedHudDocument_->isDirty()) {
      error = "Save or undo the open HUD before switching HUD documents.";
      return false;
    }
    openedHudDocument_.reset();
    usesOpenedHudDocument_ = false;
    activateHudDocument();
    return true;
  }
  if (openedHudDocument_ && samePath(path, openedHudDocument_->path())) {
    activateHudDocument();
    return true;
  }
  if (openedHudDocument_ && openedHudDocument_->isDirty()) {
    error = "Save or undo the open HUD before switching HUD documents.";
    return false;
  }
  EditorHudDocument document;
  if (!document.open(path, error))
    return false;
  openedHudDocument_ = std::move(document);
  usesOpenedHudDocument_ = true;
  activateHudDocument();
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
  if (terrainAssetDocument_ &&
      (terrainAssetDocument_->isDirty() || terrainAssetCachePending_ ||
       terrainAssetDraft_ ||
       (terrainAssetBinding_ && terrainAuthoring_->hasDraftChanges()))) {
    error = "Save or undo the Terrain asset and generate or discard its draft "
            "before refreshing the project.";
    return false;
  }
  if (!restoreTerrainPreview(error))
    return false;
  if (terrainAssetDocument_)
    leaveTerrainAssetDocument();
  terrainAssetDocument_.reset();
  terrainAssetSurface_.reset();
  terrainAssetDraft_.reset();
  terrainAuthoring_->unbind();
  if (sceneDocument_.isDirty() || projectDocument_.isDirty() ||
      (hudDocument_ && hudDocument_->isDirty()) ||
      (openedHudDocument_ && openedHudDocument_->isDirty())) {
    error =
        "The scene or project has unsaved changes. Save or undo them before "
        "refreshing.";
    return false;
  }
  try {
    assets::prepareTerrainAssets(
        loadAssetRegistry(project_->project.projectDirectory));
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }
  auto loaded = runtime::loadProject(projectPath_, error);
  if (!loaded)
    return false;
  const std::string activeSceneId = project_->world.activeSceneId;
  if (!editingPrefab_ && !activeSceneId.empty() &&
      activeSceneId != loaded->project.mainScene) {
    auto activeWorld =
        runtime::loadScene(loaded->project, activeSceneId, error);
    if (!activeWorld)
      return false;
    loaded->world = std::move(*activeWorld);
  }
  if (!sceneDocument_.reload(error))
    return false;
  if (!projectDocument_.reload(error))
    return false;
  project_ = std::move(loaded);
  if (!rebuildWorld(error))
    return false;
  if (!loadHudDocument(error))
    return false;
  viewportTool_.cancelDrag();
  viewportTool2D_.cancelDrag();
  updateSceneDomain(false);
  discoverSources();
  refreshAssetIndex();
  refreshTerrainPresets();
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
  if (!terrainReady(error))
    return false;
  if (terrainEditingAsset()) {
    if (!saveTerrainAsset(error))
      return false;
    return activeDocument_ != EditorWorkspaceDocument::Scene ||
           !sceneDocument_.isDirty() || sceneDocument_.save(error);
  }
  if (activeDocument_ == EditorWorkspaceDocument::Hud && hudDirty())
    return saveHud(error);
  if (!sceneDocument_.save(error)) {
    syncEditorDiagnostic();
    return false;
  }
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::saveAll(std::string &error) {
  if (!terrainReady(error))
    return false;
  if (terrainAssetDocument_ &&
      (terrainAssetDocument_->isDirty() || terrainAssetCachePending_) &&
      !saveTerrainAsset(error))
    return false;
  if (hudDocument_ && hudDocument_->isDirty()) {
    if (!hudDocument_->save(error))
      return false;
    if (isUiPrefabFile(hudDocument_->path()))
      refreshAssetMetadata();
  }
  if (openedHudDocument_ && openedHudDocument_->isDirty()) {
    if (!openedHudDocument_->save(error))
      return false;
    if (isUiPrefabFile(openedHudDocument_->path()))
      refreshAssetMetadata();
  }
  if (sceneDocument_.isDirty() && !sceneDocument_.save(error))
    return false;
  if (projectDocument_.isDirty() && !saveProject(error))
    return false;
  return true;
}

std::vector<EditorRecoveryDocument> EditorWorkspace::dirtyDocuments() const {
  std::vector<EditorRecoveryDocument> documents;
  if (sceneDocument_.isDirty())
    documents.push_back({.path = sceneDocument_.path(),
                         .kind = editingPrefab_ ? "prefab" : "scene",
                         .content = sceneDocument_.json()});
  if (projectDocument_.isDirty())
    documents.push_back({.path = projectDocument_.path(),
                         .kind = "project",
                         .content = projectDocument_.json()});
  if (hudDocument_ && hudDocument_->isDirty())
    documents.push_back({.path = hudDocument_->path(),
                         .kind = "hud",
                         .content = hudDocument_->json()});
  if (openedHudDocument_ && openedHudDocument_->isDirty())
    documents.push_back({.path = openedHudDocument_->path(),
                         .kind = "hud",
                         .content = openedHudDocument_->json()});
  if (terrainAssetDocument_ && terrainAssetDocument_->isDirty())
    documents.push_back({.path = terrainAssetDocument_->path(),
                         .kind = "terrain-asset",
                         .content = terrainAssetDocument_->json()});
  return documents;
}

bool EditorWorkspace::applyRecovery(const EditorRecoverySnapshot &snapshot,
                                    std::string &error) {
  const auto terrain = std::ranges::find(snapshot.documents, "terrain-asset",
                                         &EditorRecoveryDocument::kind);
  const auto prefab = std::ranges::find(snapshot.documents, "prefab",
                                        &EditorRecoveryDocument::kind);
  if (prefab != snapshot.documents.end()) {
    if (hasUnsavedChanges()) {
      error = "Save or undo current changes before restoring a prefab session.";
      return false;
    }
    EditorWorkspace recovered;
    if (!recovered.open(projectPath_, error) ||
        !recovered.openPrefabDocument(prefab->path, error))
      return false;
    if (terrain != snapshot.documents.end() &&
        !recovered.openTerrainAssetDocument(terrain->path, error))
      return false;
    auto staged = snapshot;
    for (auto &document : staged.documents)
      if (document.kind == "prefab")
        document.kind = "scene";
    if (!recovered.applyRecovery(staged, error))
      return false;
    *this = std::move(recovered);
    return true;
  }
  if (terrain != snapshot.documents.end() &&
      (!terrainAssetDocument_ ||
       !samePath(terrain->path, terrainAssetDocument_->path()))) {
    if (hasUnsavedChanges()) {
      error = "Save or undo current changes before restoring a Terrain asset.";
      return false;
    }
    EditorWorkspace recovered;
    if (!recovered.open(projectPath_, error) ||
        !recovered.openTerrainAssetDocument(terrain->path, error) ||
        !recovered.applyRecovery(snapshot, error))
      return false;
    *this = std::move(recovered);
    return true;
  }
  EditorSceneDocument sceneBefore = sceneDocument_;
  EditorProjectDocument projectBefore = projectDocument_;
  std::optional<EditorHudDocument> hudBefore = hudDocument_;
  std::optional<EditorHudDocument> openedHudBefore = openedHudDocument_;
  std::optional<EditorTerrainAssetDocument> terrainBefore =
      terrainAssetDocument_;
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
    else if (document.kind == "hud" && openedHudDocument_ &&
             samePath(document.path, openedHudDocument_->path()))
      restored = openedHudDocument_->restore(document.content, error);
    else if (document.kind == "hud") {
      EditorHudDocument recovered;
      if (recovered.open(document.path, error) &&
          recovered.restore(document.content, error)) {
        openedHudDocument_ = std::move(recovered);
        restored = true;
      }
    } else if (document.kind == "terrain-asset" && terrainAssetDocument_ &&
               samePath(document.path, terrainAssetDocument_->path())) {
      restored = terrainAssetDocument_->restore(document.content, error);
    } else
      continue;
    if (!restored) {
      sceneDocument_ = std::move(sceneBefore);
      projectDocument_ = std::move(projectBefore);
      hudDocument_ = std::move(hudBefore);
      openedHudDocument_ = std::move(openedHudBefore);
      terrainAssetDocument_ = std::move(terrainBefore);
      return false;
    }
  }
  bool previewRestored = true;
  if (terrainAssetDocument_)
    previewRestored =
        refreshTerrainAssetPreview(terrainAssetDocument_->recipe(), error) &&
        rebuildParkedSceneWorld(error);
  else if (!sceneDocument_.path().empty())
    previewRestored = rebuildWorld(error);
  if (!previewRestored) {
    sceneDocument_ = std::move(sceneBefore);
    projectDocument_ = std::move(projectBefore);
    hudDocument_ = std::move(hudBefore);
    openedHudDocument_ = std::move(openedHudBefore);
    terrainAssetDocument_ = std::move(terrainBefore);
    return false;
  }
  syncHudPreview();
  return true;
}

bool EditorWorkspace::resolveExternalChange(
    const ExternalChangeDecision decision,
    const std::filesystem::path &copyPath, std::string &error) {
  if (!restoreTerrainPreview(error))
    return false;
  terrainAuthoring_->unbind();
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
    if (!rebuildWorld(error))
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
  }
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::undo(std::string &error) {
  if (!terrainReady(error))
    return false;
  if (terrainEditingAsset() &&
      (activeDocument_ == EditorWorkspaceDocument::TerrainAsset ||
       lastEditTerrainAsset_))
    return terrainAssetHistory(false, error);
  EditorHudDocument *hud = activeHudDocument();
  if (activeDocument_ == EditorWorkspaceDocument::Hud && hud &&
      hud->canUndo()) {
    if (!hud->undo(error))
      return false;
    syncHudPreview();
    return true;
  }
  if (sceneDocument_.nextTerrainUndo())
    return terrainHistory(false, error);
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
  if (!terrainReady(error))
    return false;
  if (terrainEditingAsset() &&
      (activeDocument_ == EditorWorkspaceDocument::TerrainAsset ||
       lastEditTerrainAsset_))
    return terrainAssetHistory(true, error);
  EditorHudDocument *hud = activeHudDocument();
  if (activeDocument_ == EditorWorkspaceDocument::Hud && hud &&
      hud->canRedo()) {
    if (!hud->redo(error))
      return false;
    syncHudPreview();
    return true;
  }
  if (sceneDocument_.nextTerrainRedo())
    return terrainHistory(true, error);
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
  target = resolveSceneTarget(std::move(target));
  if (target.component == "Terrain3D" && target.field == "recipe") {
    const auto *existing =
        runtime::findEntity(project_->world, target.entityId);
    const auto *native =
        existing ? existing->component<runtime::Terrain3DComponent>() : nullptr;
    if (native && !native->asset.empty()) {
      error = "Edit this terrain in its asset source.";
      return false;
    }
    if (native && native->recipe.is_null())
      return mutateAndRebuild(
          [&](EditorSceneDocument &document, std::string &issue) {
            return document.setValue(target, value, continuous, issue);
          },
          error);
    if (!restoreTerrainPreview(error))
      return false;
    const auto *owner = runtime::findEntity(project_->world, target.entityId);
    const auto *terrain =
        owner ? owner->component<runtime::Terrain3DComponent>() : nullptr;
    if (!terrain) {
      error = "The terrain owner no longer exists.";
      return false;
    }
    try {
      auto update = updateEditorTerrain(
          terrain->recipe, value,
          currentEditorTerrain(project_->world, target.entityId), {}, {},
          error);
      if (!update)
        return false;
      EditorSceneDocument before = sceneDocument_;
      if (!sceneDocument_.setTerrainRecipe(target, value, update->patch,
                                           continuous, error))
        return false;
      if (!installEditorTerrain(project_->world, target.entityId, value,
                                *update, error)) {
        sceneDocument_ = std::move(before);
        return false;
      }
      syncTerrainAuthoring();
      lastEditTerrainAsset_ = false;
      syncEditorDiagnostic();
      return true;
    } catch (const std::exception &exception) {
      error = exception.what();
      return false;
    }
  }
  if (target.component == "PrefabPlacement3D" ||
      target.component == "Masonry3D" || target.component == "Terrain3D" ||
      (target.component.empty() && target.field == "enabled"))
    return mutateAndRebuild(
        [&](EditorSceneDocument &document, std::string &issue) {
          return document.setValue(target, value, continuous, issue);
        },
        error);
  if (target.isPrefabOverride()) {
    EditorSceneDocument before = sceneDocument_;
    if (!sceneDocument_.setValue(target, std::move(value), continuous, error)) {
      syncEditorDiagnostic();
      return false;
    }
    const nlohmann::json *authoredValue =
        valueInDocument(sceneDocument_.json(), target);
    if (authoredValue == nullptr ||
        !applyEditorPreviewValue(project_->world, target, *authoredValue,
                                 error)) {
      sceneDocument_ = std::move(before);
      workspaceOperationError_ = error;
      syncEditorDiagnostic();
      return false;
    }
    workspaceOperationError_.clear();
    lastEditTerrainAsset_ = false;
    syncEditorDiagnostic();
    return true;
  }
  if (!sceneDocument_.setValue(std::move(target), std::move(value), continuous,
                               error)) {
    syncEditorDiagnostic();
    return false;
  }
  workspaceOperationError_.clear();
  lastEditTerrainAsset_ = false;
  syncChangedEntity();
  syncEditorDiagnostic();
  return true;
}

bool EditorWorkspace::editValues(std::vector<SceneValueTarget> targets,
                                 nlohmann::json value, std::string &error) {
  for (SceneValueTarget &target : targets)
    target = resolveSceneTarget(std::move(target));
  return mutateAndRebuild(
      [targets = std::move(targets), value = std::move(value)](
          EditorSceneDocument &document, std::string &mutationError) mutable {
        return document.setValues(std::move(targets), std::move(value),
                                  mutationError);
      },
      error);
}

bool EditorWorkspace::removeValue(SceneValueTarget target, std::string &error) {
  target = resolveSceneTarget(std::move(target));
  return mutateAndRebuild(
      [target = std::move(target)](EditorSceneDocument &document,
                                   std::string &mutationError) mutable {
        return document.removeValue(std::move(target), mutationError);
      },
      error);
}

SceneValueTarget
EditorWorkspace::authoredTarget(SceneValueTarget target) const {
  return resolveSceneTarget(std::move(target));
}

bool EditorWorkspace::hasExplicitValue(SceneValueTarget target) const {
  return valueInDocument(sceneDocument_.json(),
                         resolveSceneTarget(std::move(target))) != nullptr;
}

bool EditorWorkspace::createEntity(std::string &error,
                                   std::optional<std::string> parent,
                                   const EditorEntityKind kind,
                                   std::optional<runtime::Vec3> worldPosition) {
  if (kind != EditorEntityKind::Empty &&
      viewDimension() != EditorSceneViewDimension::ThreeDimensional) {
    error = "3D primitives require a 3D scene view.";
    return false;
  }
  if (project_ && viewDimension() == EditorSceneViewDimension::ThreeDimensional &&
      !worldPosition) {
    worldPosition = sceneDropWorldPosition3D(
        sceneView_.camera(), project_->world, {0.5F, 0.5F}, {1.0F, 1.0F});
  }
  if (worldPosition && kind != EditorEntityKind::Empty &&
      kind != EditorEntityKind::Plane)
    worldPosition->y += 0.5F;
  if (worldPosition && parent && project_) {
    const runtime::Entity *parentEntity =
        runtime::findEntity(project_->world, *parent);
    const auto transform = parentEntity
        ? runtime::resolveWorldTransform3D(project_->world, *parentEntity)
        : std::nullopt;
    if (!transform) {
      error = "The parent has no resolvable 3D transform.";
      return false;
    }
    worldPosition = runtime::inverseTransformPoint3D(*transform, *worldPosition);
  }
  if (!mutateAndRebuild(
          [parent = std::move(parent), kind, worldPosition](
              EditorSceneDocument &document, std::string &mutationError) mutable {
            return document.createEntity(mutationError, std::move(parent),
                                         kind, worldPosition);
          },
          error))
    return false;
  selectEntity(std::string(sceneDocument_.lastChangedEntityId()));
  return true;
}

bool EditorWorkspace::createPresetEntity(std::string_view preset,
                                         std::string &error) {
  if (!mutateAndRebuild(
          [preset = std::string(preset)](EditorSceneDocument &document,
                                         std::string &mutationError) {
            return document.createPresetEntity(preset, mutationError);
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

bool EditorWorkspace::unpackPreset(const std::string_view id,
                                   std::string &error) {
  return mutateAndRebuild(
      [&](EditorSceneDocument &document, std::string &failure) {
        return document.unpackPreset(id, failure);
      },
      error);
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
  const bool revealSelection = isEntitySelected(id);
  const bool changed = mutateAndRebuild(
      [id = std::string(id), newParent = std::move(newParent)](
          EditorSceneDocument &document, std::string &mutationError) mutable {
        return document.reparent(id, std::move(newParent), mutationError);
      },
      error);
  if (changed && revealSelection)
    ++selectionRevision_;
  return changed;
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
  EditorHudDocument *hud = activeHudDocument();
  if (hud == nullptr) {
    error = "The current scene has no authored HUD document.";
    return false;
  }
  std::string parent(selectedHudNodeId_);
  if (parent.empty() && !hud->preview().nodes.empty())
    parent = hud->preview().nodes.front().id;
  std::string created;
  if (!hud->createNode(type, parent, created, error))
    return false;
  syncHudPreview();
  selectHudNode(std::move(created));
  return true;
}

bool EditorWorkspace::deleteSelectedHudNode(std::string &error) {
  EditorHudDocument *hud = activeHudDocument();
  if (hud == nullptr || selectedHudNodeId_.empty()) {
    error = "Select an authored HUD element first.";
    return false;
  }
  if (!hud->deleteNode(selectedHudNodeId_, error))
    return false;
  clearHudSelection();
  syncHudPreview();
  return true;
}

bool EditorWorkspace::createHudPrefabInstance(const std::string_view reference,
                                              std::string &error) {
  auto *hud = activeHudDocument();
  if (!hud) {
    error = "Open a HUD before adding a UI prefab.";
    return false;
  }
  std::string parent(selectedHudNodeId_);
  if (parent.empty() && !hud->preview().nodes.empty())
    parent = hud->preview().nodes.front().id;
  std::string created;
  if (!hud->createPrefabInstance(reference, parent, created, error))
    return false;
  syncHudPreview();
  selectHudNode(std::move(created));
  return true;
}

bool EditorWorkspace::placeHudModule(const EditorModule &module,
                                     const runtime::Vec2 authoredPoint,
                                     const std::string_view targetId,
                                     std::string &error) {
  auto *hud = activeHudDocument();
  if (!hud || activeDocument_ != EditorWorkspaceDocument::Hud) {
    error = "Open a HUD before placing a module.";
    return false;
  }
  if (module.kind == EditorModuleKind::TerrainNode) {
    error = "Terrain nodes belong on the terrain graph canvas.";
    return false;
  }
  const runtime::ui::UiDocument &preview = hud->preview();
  const runtime::ui::UiNode *parent = nullptr;
  if (!targetId.empty()) {
    parent = findEditorHudNode(preview, targetId);
    while (parent && (!hud->authoredNode(parent->id) ||
                      hud->authoredNode(parent->id)->contains("prefab") ||
                      (parent->type != "container" && parent->type != "panel" &&
                       parent->type != "scroll" && parent->type != "list" &&
                       parent->type != "modal")))
      parent = findEditorHudNode(preview, parent->parent);
  }
  if (!parent && !preview.nodes.empty())
    parent = &preview.nodes.front();
  if (!parent) {
    error = "The HUD has no root for the new module.";
    return false;
  }
  const runtime::Vec2 position{
      authoredPoint.x - parent->resolved.x - parent->layout.padding.left,
      authoredPoint.y - parent->resolved.y - parent->layout.padding.top};
  std::string created;
  const bool placed =
      module.kind == EditorModuleKind::HudElement
          ? hud->createNode(module.value, parent->id, created, error, position)
          : hud->createPrefabInstance(module.value, parent->id, created, error,
                                      position);
  if (!placed)
    return false;
  syncHudPreview();
  selectHudNode(std::move(created));
  return true;
}

bool EditorWorkspace::setHudNodeAnchors(const std::string_view id,
                                        const runtime::Vec2 minimum,
                                        const runtime::Vec2 maximum,
                                        std::string &error) {
  auto *hud = activeHudDocument();
  if (!hud) {
    error = "Open a HUD before changing its layout.";
    return false;
  }
  if (!hud->setNodeAnchors(id, minimum, maximum, error))
    return false;
  syncHudPreview();
  return true;
}

bool EditorWorkspace::setHudCanvasSize(const runtime::Vec2 size,
                                       std::string &error) {
  auto *hud = activeHudDocument();
  if (!hud) {
    error = "Open a HUD before changing its canvas.";
    return false;
  }
  if (!hud->setCanvasSize(size, error))
    return false;
  syncHudPreview();
  return true;
}

bool EditorWorkspace::reparentHudNode(const std::string_view id,
                                      const std::string_view parent,
                                      std::string &error) {
  auto *hud = activeHudDocument();
  if (!hud) {
    error = "Open a HUD before moving UI elements.";
    return false;
  }
  if (!hud->reparentNode(id, parent, error))
    return false;
  syncHudPreview();
  selectHudNode(std::string(id));
  return true;
}

bool EditorWorkspace::duplicateHudNode(const std::string_view id,
                                       std::string &error) {
  auto *hud = activeHudDocument();
  if (!hud) {
    error = "Open a HUD before duplicating UI elements.";
    return false;
  }
  std::string created;
  if (!hud->duplicateNode(id, created, error))
    return false;
  syncHudPreview();
  selectHudNode(std::move(created));
  return true;
}

bool EditorWorkspace::setHudNodeField(const std::string_view id,
                                      const std::string_view field,
                                      nlohmann::json value, std::string &error,
                                      bool continuous) {
  EditorHudDocument *hud = activeHudDocument();
  if (hud == nullptr) {
    error = "The current scene has no authored HUD document.";
    return false;
  }
  if (!hud->setNodeField(id, field, std::move(value), error, continuous))
    return false;
  syncHudPreview();
  return true;
}

void EditorWorkspace::endHudContinuousEdit() {
  if (auto *hud = activeHudDocument())
    hud->endContinuousEdit();
}

bool EditorWorkspace::saveHud(std::string &error) {
  EditorHudDocument *hud = activeHudDocument();
  if (hud == nullptr)
    return true;
  if (!hud->save(error))
    return false;
  if (isUiPrefabFile(hud->path()))
    refreshAssetMetadata();
  else
    refreshDiagnostics();
  return true;
}

bool EditorWorkspace::refreshCleanHudDocument(
    const std::filesystem::path &path, std::string &error) {
  if (!project_) {
    error = "Open a project before refreshing linked HUD data.";
    return false;
  }
  std::optional<EditorHudDocument> nextLinked;
  std::optional<EditorHudDocument> nextOpened;
  const auto prepare = [&](const std::optional<EditorHudDocument> &current,
                           std::optional<EditorHudDocument> &next) {
    if (!current || !samePath(current->path(), path))
      return true;
    if (current->isDirty()) {
      error = "HUD source was saved in another session while this workspace "
              "has unsaved HUD edits: " + path.string();
      return false;
    }
    EditorHudDocument candidate;
    if (!candidate.open(current->path(), error))
      return false;
    if (candidate.json() != current->json())
      next = std::move(candidate);
    return true;
  };
  if (!prepare(hudDocument_, nextLinked) ||
      !prepare(openedHudDocument_, nextOpened))
    return false;
  if (!nextLinked && !nextOpened)
    return true;
  if (nextLinked) {
    hudDocument_ = std::move(nextLinked);
    project_->world.ui = hudDocument_->preview();
    project_->world.hudCanvasSize = project_->world.ui.canvasSize;
  }
  if (nextOpened)
    openedHudDocument_ = std::move(nextOpened);
  const auto &nodes = displayedHud().nodes;
  std::erase_if(selectedHudNodeIds_, [&](const std::string &id) {
    return std::ranges::find(nodes, id, &runtime::ui::UiNode::id) == nodes.end();
  });
  if (!selectedHudNodeId_.empty() && selectedHudNode() == nullptr)
    selectedHudNodeId_ = selectedHudNodeIds_.empty()
                             ? std::string{}
                             : selectedHudNodeIds_.back();
  return true;
}

bool EditorWorkspace::updateViewportTool(const EditorViewportToolInput &input,
                                         std::string &error) {
  syncTerrainAuthoring();
  if (terrainAuthoring_->brushActive()) {
    const auto hit = pickEditorTerrain(project_->world, selectedEntityId(),
                                       terrainAuthoring_->surface(),
                                       sceneView_.camera(), input);
    if (!terrainAuthoring_->update(input, hit, error))
      return false;
    if (terrainAuthoring_->takeRollback())
      return restoreTerrainPreview(error);
    return true;
  }
  return applyViewportAction(
      viewportTool_.update(project_->world, selectedEntityId(), sceneView_,
                           input),
      [this] { viewportTool_.cancelDrag(); }, error);
}

bool EditorWorkspace::pinTerrainAuthoring(const std::string_view entityId,
                                          std::string &error) {
  if (!project_) {
    error = "Open a project before pinning a terrain graph.";
    return false;
  }
  const auto *owner =
      runtime::findEntity(project_->world, std::string(entityId));
  const auto *terrain =
      owner ? owner->component<runtime::Terrain3DComponent>() : nullptr;
  if (!terrain || (terrain->asset.empty() && terrain->recipe.is_null())) {
    error = "Choose a configured Terrain3D owner for the graph.";
    return false;
  }
  if (const auto *asset = findAsset(assetIndex_.registry(), terrain->asset);
      asset && assets::isPreparedTerrainAsset(*asset)) {
    error = "Cooked terrain is read-only. Open its authored source project to edit the graph.";
    return false;
  }
  const auto ownerDocument =
      activeDocument_ == EditorWorkspaceDocument::TerrainAsset &&
              terrainAssetDocument_
          ? terrainAssetDocument_->path()
          : sceneDocument_.path();
  const bool changingPinnedTarget =
      terrainAuthoringTarget_ &&
      (terrainAuthoringTarget_->document != ownerDocument ||
       terrainAuthoringTarget_->entityId != entityId);
  if (changingPinnedTarget && terrainAuthoring_->hasDraftChanges()) {
    error = "Generate or discard the current terrain graph draft before "
            "opening another terrain graph.";
    return false;
  }
  if (changingPinnedTarget && !terrainReady(error))
    return false;
  if (terrainAuthoring_->entityId() != entityId && !terrainReady(error))
    return false;
  const auto previous = terrainAuthoringTarget_;
  terrainAuthoringTarget_ =
      EditorTerrainAuthoringTarget{ownerDocument, std::string(entityId)};
  syncTerrainAuthoring();
  if (terrainAuthoring_->entityId() == entityId)
    return true;
  error = workspaceOperationError_.empty()
              ? "Could not bind the terrain graph target."
              : workspaceOperationError_;
  terrainAuthoringTarget_ = previous;
  syncTerrainAuthoring();
  return false;
}

bool EditorWorkspace::unpinTerrainAuthoring(std::string &error) {
  if (!terrainAuthoringTarget_)
    return true;
  if (terrainAuthoring_->hasDraftChanges()) {
    error = "Generate or discard the terrain graph draft before closing it.";
    return false;
  }
  if (!terrainReady(error))
    return false;
  if (terrainAuthoring_->entityId() != selectedEntityId() &&
      !restoreTerrainPreview(error))
    return false;
  terrainAuthoringTarget_.reset();
  syncTerrainAuthoring();
  return true;
}

void EditorWorkspace::syncTerrainAuthoring() {
  if (!project_)
    return;
  const auto ownerDocument =
      activeDocument_ == EditorWorkspaceDocument::TerrainAsset &&
              terrainAssetDocument_
          ? terrainAssetDocument_->path()
          : sceneDocument_.path();
  if (terrainAuthoringTarget_ &&
      terrainAuthoringTarget_->document != ownerDocument)
    terrainAuthoringTarget_.reset();
  const std::string ownerId = terrainAuthoringTarget_
                                  ? terrainAuthoringTarget_->entityId
                                  : std::string(selectedEntityId());
  const bool brushTargetSelected =
      (activeDocument_ == EditorWorkspaceDocument::Scene ||
       activeDocument_ == EditorWorkspaceDocument::TerrainAsset) &&
      viewDimension_ == EditorSceneViewDimension::ThreeDimensional &&
      ownerId == selectedEntityId();
  terrainAuthoring_->setBrushTargetSelected(brushTargetSelected);
  if (!brushTargetSelected && terrainAuthoring_->stroking()) {
    std::string error;
    if (!restoreTerrainPreview(error)) {
      workspaceOperationError_ = std::move(error);
      return;
    }
  }
  const runtime::Entity *entity = runtime::findEntity(project_->world, ownerId);
  const auto *terrain =
      entity ? entity->component<runtime::Terrain3DComponent>() : nullptr;
  const bool changingSelection =
      !entity || terrainAuthoring_->entityId() != entity->id;
  const auto retainAssetDraft = [&] {
    if (!terrainAssetBinding_)
      return;
    if (terrainAuthoring_->hasDraftChanges())
      terrainAssetDraft_ = terrainAuthoring_->draft();
    else
      terrainAssetDraft_.reset();
  };
  if ((!terrainAuthoringTarget_ &&
       (activeDocument_ != EditorWorkspaceDocument::Scene &&
        activeDocument_ != EditorWorkspaceDocument::TerrainAsset)) ||
      (!terrainAuthoringTarget_ &&
       viewDimension_ != EditorSceneViewDimension::ThreeDimensional) ||
      !terrain || (terrain->asset.empty() && terrain->recipe.is_null())) {
    std::string error;
    if (!restoreTerrainPreview(error)) {
      workspaceOperationError_ = std::move(error);
      return;
    }
    retainAssetDraft();
    terrainAuthoring_->unbind();
    if (terrainAssetBinding_) {
      terrainAssetBinding_ = false;
      configureTerrainInputResolver();
    }
    return;
  }
  if (terrainAuthoring_->entityId() != entity->id) {
    std::string error;
    if (!restoreTerrainPreview(error)) {
      workspaceOperationError_ = std::move(error);
      return;
    }
    // Restoring chunks can invalidate entity addresses.
    entity = runtime::findEntity(project_->world, ownerId);
    terrain =
        entity ? entity->component<runtime::Terrain3DComponent>() : nullptr;
    if (!terrain)
      return;
  }
  retainAssetDraft();
  if (!terrain->asset.empty()) {
    if (const auto *asset = findAsset(assetIndex_.registry(), terrain->asset);
        asset && assets::isPreparedTerrainAsset(*asset)) {
      terrainAuthoring_->unbind();
      terrainAssetBinding_ = false;
      configureTerrainInputResolver();
      return;
    }
    std::string error;
    if (!bindTerrainAsset(terrain->asset, error)) {
      terrainAuthoring_->unbind();
      terrainAssetBinding_ = false;
      configureTerrainInputResolver();
      workspaceOperationError_ = std::move(error);
      syncEditorDiagnostic();
      return;
    }
    if (!terrainAssetBinding_) {
      terrainAssetBinding_ = true;
      configureTerrainInputResolver();
    }
    terrainAuthoring_->bind(terrainAssetDocument_->path().string(), entity->id,
                            terrainAssetDocument_->recipe(),
                            currentEditorTerrain(project_->world, entity->id),
                            terrainAssetDocument_->id());
    if (changingSelection && terrainAssetDraft_)
      terrainAuthoring_->draft() = *terrainAssetDraft_;
    if (!terrainAssetSurface_)
      terrainAssetSurface_ = currentEditorTerrain(project_->world, entity->id);
    return;
  }
  if (terrainAssetBinding_) {
    terrainAssetBinding_ = false;
    configureTerrainInputResolver();
  }
  const auto target = resolveSceneTarget(
      {.entityId = entity->id, .component = "Terrain3D", .field = "recipe"});
  const auto *sourceRecipe = valueInDocument(sceneDocument_.json(), target);
  const auto &baseline =
      sourceRecipe && !target.isPrefabOverride()
          ? *sourceRecipe
          : (terrainPreview_ && terrainPreview_->entityId == entity->id &&
                     terrainPreview_->recipe == terrain->recipe
                 ? terrainPreview_->before
                 : terrain->recipe);
  terrainAuthoring_->bind(sceneDocument_.path().string(), entity->id, baseline,
                          currentEditorTerrain(project_->world, entity->id));
}

bool EditorWorkspace::pollTerrainAuthoring(std::string &error) {
  syncTerrainAuthoring();
  auto commit = terrainAuthoring_->poll(error);
  if (!commit) {
    if (!error.empty()) {
      std::string ignored;
      (void)restoreTerrainPreview(ignored);
    }
    return error.empty();
  }
  const runtime::Entity *entity =
      runtime::findEntity(project_->world, commit->entityId);
  const auto *terrain =
      entity ? entity->component<runtime::Terrain3DComponent>() : nullptr;
  if (commit->restore) {
    return restoreTerrainPreview(error);
  }
  if (terrainEditingAsset())
    return pollTerrainAssetAuthoring(std::move(*commit), error);
  const auto target = resolveSceneTarget({.entityId = commit->entityId,
                                          .component = "Terrain3D",
                                          .field = "recipe"});
  const auto baseline = effectiveTerrainRecipe(target, error);
  if (!baseline)
    return false;
  if (commit->document != sceneDocument_.path().string() || !terrain ||
      commit->entityId != entity->id || commit->before != *baseline) {
    error =
        "Terrain changed while generating; the generated result was discarded.";
    std::string ignored;
    (void)restoreTerrainPreview(ignored);
    return false;
  }
  if (!commit->final) {
    const auto original =
        terrainPreview_
            ? terrainPreviewSurface_
            : currentEditorTerrain(project_->world, commit->entityId);
    auto historyPatch = commit->patch;
    try {
      if (terrainPreview_)
        historyPatch =
            mergeEditorTerrainPatches(terrainPreview_->patch, commit->patch);
    } catch (const std::exception &exception) {
      error = exception.what();
      std::string ignored;
      (void)restoreTerrainPreview(ignored);
      return false;
    }
    if (!installEditorTerrain(project_->world, commit->entityId, commit->recipe,
                              {commit->surface, commit->patch}, error)) {
      std::string rollbackError;
      (void)restoreTerrainPreview(rollbackError);
      return false;
    }
    commit->patch = std::move(historyPatch);
    terrainPreview_ = std::move(commit);
    terrainPreviewSurface_ = original;
    return true;
  }
  try {
    publishEditorTerrain(commit->recipe, commit->surface);
  } catch (const std::exception &exception) {
    error = exception.what();
    std::string ignored;
    (void)restoreTerrainPreview(ignored);
    return false;
  }
  sceneDocument_.endContinuousEdit();
  EditorSceneDocument before = sceneDocument_;
  if (!sceneDocument_.setTerrainRecipe(target, commit->recipe, commit->patch,
                                       false, error)) {
    std::string ignored;
    (void)restoreTerrainPreview(ignored);
    return false;
  }
  const bool alreadyPublished =
      terrain->generated == commit->surface->heightField() &&
      terrain->recipe == commit->recipe;
  if (!alreadyPublished &&
      !installEditorTerrain(project_->world, commit->entityId, commit->recipe,
                            {commit->surface, commit->publicationPatch
                                                  ? commit->publicationPatch
                                                  : commit->patch},
                            error)) {
    sceneDocument_ = std::move(before);
    std::string ignored;
    (void)restoreTerrainPreview(ignored);
    return false;
  }
  terrainPreview_.reset();
  terrainPreviewSurface_.reset();
  syncTerrainAuthoring();
  syncEditorDiagnostic();
  return true;
}

bool EditorWorkspace::terrainReady(std::string &error) const {
  if (terrainAuthoring_->pendingEdits() || terrainPreview_) {
    error = "Finish or cancel the terrain stroke and wait for terrain "
            "collision before Save or Play.";
    return false;
  }
  return true;
}

bool EditorWorkspace::restoreTerrainPreview(std::string &error) {
  terrainAuthoring_->cancel();
  (void)terrainAuthoring_->takeRollback();
  if (!terrainPreview_)
    return true;
  if (terrainAssetDocument_ &&
      terrainPreview_->document == terrainAssetDocument_->path().string()) {
    const auto before = terrainAssetDocument_->recipe();
    if (!refreshTerrainAssetPreview(before, error))
      return false;
    terrainPreview_.reset();
    terrainPreviewSurface_.reset();
    return true;
  }
  if (project_ && terrainPreview_->document == sceneDocument_.path().string()) {
    const auto *owner =
        runtime::findEntity(project_->world, terrainPreview_->entityId);
    const auto *terrain =
        owner ? owner->component<runtime::Terrain3DComponent>() : nullptr;
    // A source reload/rebuild has already replaced the preview. Never overwrite
    // it.
    if (terrain &&
        terrain->generated == terrainPreview_->surface->heightField()) {
      const auto target =
          resolveSceneTarget({.entityId = terrainPreview_->entityId,
                              .component = "Terrain3D",
                              .field = "recipe"});
      const auto source = effectiveTerrainRecipe(target, error);
      if (!source)
        return false;
      if (*source == terrainPreview_->before) {
        if (!installEditorTerrain(
                project_->world, terrainPreview_->entityId, *source,
                {terrainPreviewSurface_, terrainPreview_->patch}, error))
          return false;
      } else {
        // An authored change invalidated the transient recipe while it was
        // visible. Restore the new source, rather than publishing the old one.
        try {
          const auto update = updateEditorTerrain(
              terrain->recipe, *source,
              currentEditorTerrain(project_->world, terrainPreview_->entityId),
              {}, {}, error);
          if (!update ||
              !installEditorTerrain(project_->world, terrainPreview_->entityId,
                                    *source, *update, error))
            return false;
        } catch (const std::exception &exception) {
          error = exception.what();
          return false;
        }
      }
    }
  }
  terrainPreview_.reset();
  terrainPreviewSurface_.reset();
  return true;
}

bool EditorWorkspace::terrainHistory(bool forward, std::string &error) {
  const auto *command = forward ? sceneDocument_.nextTerrainRedo()
                                : sceneDocument_.nextTerrainUndo();
  const auto target = command->target;
  const auto patch = command->samplePatch;
  if (!patch) {
    error = "Terrain history is missing its native sample patch.";
    return false;
  }
  const auto current = currentEditorTerrain(project_->world, target.entityId);
  const auto surface = applyEditorTerrainPatch(current, *patch, forward, error);
  if (!surface)
    return false;
  EditorSceneDocument before = sceneDocument_;
  if (!(forward ? sceneDocument_.redo(error) : sceneDocument_.undo(error)))
    return false;
  const auto recipe = effectiveTerrainRecipe(target, error);
  try {
    if (recipe)
      publishEditorTerrain(*recipe, surface);
  } catch (const std::exception &exception) {
    sceneDocument_ = std::move(before);
    error = exception.what();
    return false;
  }
  if (!recipe || !installEditorTerrain(project_->world, target.entityId,
                                       *recipe, {surface, patch}, error)) {
    sceneDocument_ = std::move(before);
    return false;
  }
  syncTerrainAuthoring();
  syncEditorDiagnostic();
  return true;
}

void EditorWorkspace::refreshTerrainPresets() {
  // Discovery uses the asset registry the editor already holds rather than
  // re-deriving asset ids from source paths: the manifest's own id is the
  // identity loadTerrainPreset resolves, so a preset can never be listed under
  // an id the engine would then refuse to load.
  auto presets = runtime::builtinTerrainPresets();
  std::vector<std::string> failures;
  if (project_) {
    const AssetRegistry &registry = assetIndex_.registry();
    for (const auto &record : assetIndex_.assets()) {
      if (record.manifest.type != "DataAsset")
        continue;
      const auto metadata = assets::dataAssetMetadata(record.manifest);
      if (!metadata || metadata->contentType != "terrain_preset")
        continue;
      try {
        if (auto preset =
                runtime::loadTerrainPreset(registry, record.manifest.id))
          presets.push_back(std::move(*preset));
      } catch (const std::exception &exception) {
        // A preset that will not load is left out of the picker, and its
        // reason is kept so the author is told instead of silently losing it.
        failures.emplace_back(exception.what());
      }
    }
  }
  std::ranges::sort(presets, {}, &runtime::TerrainPreset::id);
  terrainAuthoring_->setPresets(std::move(presets));
  terrainPresetErrors_ = std::move(failures);
}

bool EditorWorkspace::applyTerrainPreset(std::string_view presetId,
                                         std::string &error) {
  if (!terrainReady(error))
    return false;
  const auto *candidate = terrainAuthoring_->preset(presetId);
  if (candidate == nullptr) {
    error = "Landscape preset is not available: " + std::string(presetId);
    return false;
  }
  const auto before = terrainAuthoring_->draft();
  if (!terrainAuthoring_->applyPreset(*candidate, error))
    return false;
  // Applying the same preset twice must not become an undoable edit for no
  // reason, so an unchanged draft never reaches the commit path.
  if (terrainAuthoring_->draft() == before)
    return true;
  // The draft now carries the preset's grid, so the existing resize decision
  // answers for it exactly as it would for a hand-edited size. Protection
  // snapshots still require the original grid, and "Keep radial edits" refuses
  // rather than silently invalidating them.
  return terrainAuthoring_->generate(std::nullopt, error);
}

bool EditorWorkspace::clearTerrainPreset(std::string &error) {
  if (!terrainReady(error))
    return false;
  const auto before = terrainAuthoring_->draft();
  if (!terrainAuthoring_->clearPreset(error))
    return false;
  if (terrainAuthoring_->draft() == before)
    return true;
  // Provenance is a generation input, so removing it rebuilds the field and
  // goes through the same undoable recipe command as any other generation
  // change.
  return terrainAuthoring_->generate(std::nullopt, error);
}

std::optional<nlohmann::json>
EditorWorkspace::effectiveTerrainRecipe(const SceneValueTarget &target,
                                        std::string &error) const {
  if (!target.isPrefabOverride()) {
    if (const auto *recipe = valueInDocument(sceneDocument_.json(), target))
      return *recipe;
    const auto *component =
        sceneDocument_.component(target.entityId, "Terrain3D");
    if (component)
      return defaultEditorTerrainRecipe();
    error = "The authored terrain owner no longer exists.";
    return std::nullopt;
  }
  // Resolve just the owning instance's source and overrides, without loading
  // a world or disturbing unrelated runtime/render/physics resources.
  const auto *instance =
      findPrefabInstance(sceneDocument_.json(), target.prefabInstanceId);
  if (!instance) {
    error = "The terrain prefab instance no longer exists.";
    return std::nullopt;
  }
  try {
    const auto expanded = runtime::composition::expandPrefabInstance(
        sceneDocument_.path(), *instance);
    if (!expanded.document) {
      error = expanded.diagnostics.empty()
                  ? "Could not resolve the inherited terrain recipe."
                  : expanded.diagnostics.front().message;
      return std::nullopt;
    }
    const auto *source = runtime::composition::findAuthoredEntity(
        *expanded.document, target.entityId);
    if (source) {
      const auto entity = runtime::scene_loading::parseSceneEntity(*source);
      if (const auto *terrain = entity.component<runtime::Terrain3DComponent>())
        return terrain->recipe;
    }
    error = "The inherited terrain component no longer exists.";
  } catch (const std::exception &exception) {
    error = exception.what();
  }
  return std::nullopt;
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
  if (!restoreTerrainPreview(error))
    return false;
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
  lastEditTerrainAsset_ = false;
  syncEditorDiagnostic();
  return true;
}

void EditorWorkspace::syncChangedEntity() {
  const std::string_view changed = sceneDocument_.lastChangedEntityId();
  const auto flat = runtime::composition::flattenEntityHierarchy(
      sceneDocument_.json()["entities"]);
  const nlohmann::json *authored =
      runtime::composition::findAuthoredEntity(flat, changed);
  if (authored == nullptr)
    return;
  auto existing = std::ranges::find(project_->world.entities, changed,
                                    &runtime::Entity::id);
  if (existing == project_->world.entities.end())
    return;
  runtime::Entity reparsed =
      runtime::scene_loading::parseSceneEntity(*authored);
  if (auto *terrain = reparsed.component<runtime::Terrain3DComponent>();
      terrain && !terrain->asset.empty()) {
    const auto *previous = existing->component<runtime::Terrain3DComponent>();
    if (previous && previous->asset == terrain->asset) {
      terrain->recipe = previous->recipe;
      terrain->generated = previous->generated;
    }
  }
  restoreEditorDerivedState(reparsed);
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
  runtime::RuntimePrefabService previewPrefabs;
  previewPrefabs.configure(project_->project.projectDirectory);
  auto world =
      loadEntityPreview(sceneDocument_, editingPrefab_, error, &previewPrefabs);
  if (!world)
    return false;
  if (!applyTerrainAssetPreview(*world, error, &previewPrefabs))
    return false;
  for (auto &entity : world->entities)
    updateEditorMeshRevision(entity);
  updateEditorPlacementVisibility(*world);
  const auto expected = authoredHudPath();
  const bool replaceHud =
      (!expected && hudDocument_) ||
      (expected &&
       (!hudDocument_ || !samePath(*expected, hudDocument_->path())));
  std::optional<EditorHudDocument> nextHud;
  if (replaceHud) {
    if (hudDocument_ && hudDocument_->isDirty()) {
      error = "Save or undo HUD edits before switching the scene HUD.";
      return false;
    }
    if (expected) {
      EditorHudDocument candidate;
      if (!candidate.open(*expected, error))
        return false;
      nextHud = std::move(candidate);
    }
  }
  project_->world = std::move(*world);
  terrainPreviewPrefabs_ = std::move(previewPrefabs);
  if (replaceHud) {
    hudDocument_ = std::move(nextHud);
    clearHudSelection();
  }
  syncHudPreview();
  updateSceneDomain(false);
  return true;
}

SceneValueTarget
EditorWorkspace::resolveSceneTarget(SceneValueTarget target) const {
  if (target.isPrefabOverride() || !project_)
    return target;
  const runtime::Entity *entity =
      runtime::findEntity(project_->world, target.entityId);
  if (entity != nullptr && !entity->prefabInstance.empty() &&
      !entity->prefabLocalId.empty()) {
    target.prefabInstanceId = entity->prefabInstance;
    target.prefabEntityId = entity->prefabLocalId;
    return target;
  }
  // Loaded entities already carry composition provenance. Do not rescan the
  // entire source document for every ordinary Inspector field.
  if (entity != nullptr)
    return target;
  if (const auto origin = runtime::composition::prefabEntityOrigin(
          sceneDocument_.json(), target.entityId)) {
    target.prefabInstanceId = origin->instanceId;
    target.prefabEntityId = origin->localEntityId;
  }
  return target;
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
  const ValidationSummary summary = validateProjectPath(projectPath_);
  diagnostics_ = summary.diagnostics;
  const auto &scripts = scriptCatalog();
  diagnostics_.insert(diagnostics_.end(), scripts.diagnostics.begin(),
                      scripts.diagnostics.end());
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
  const auto found = std::ranges::find(displayedHud().nodes, selectedHudNodeId_,
                                       &runtime::ui::UiNode::id);
  return found == displayedHud().nodes.end() ? nullptr : &*found;
}

const EditorHudDocument *EditorWorkspace::hudDocument() const {
  if (usesOpenedHudDocument_ && openedHudDocument_)
    return &*openedHudDocument_;
  return hudDocument_ ? &*hudDocument_ : nullptr;
}

bool EditorWorkspace::setSceneHud(const std::filesystem::path &path,
                                  std::string &error) {
  if (editingPrefab_) {
    error = "Open a scene before assigning its HUD.";
    return false;
  }
  if ((hudDocument_ && hudDocument_->isDirty()) ||
      (openedHudDocument_ && openedHudDocument_->isDirty())) {
    error = "Save or undo HUD changes before changing the scene HUD.";
    return false;
  }
  auto candidate = sceneDocument_.json();
  if (path.empty())
    candidate.erase("hud");
  else {
    const auto absolute = std::filesystem::weakly_canonical(path);
    const auto relative =
        absolute.lexically_relative(project_->project.projectDirectory);
    if (relative.empty() || relative.is_absolute() ||
        *relative.begin() == ".." || !isHudFile(path)) {
      error = "Choose a HUD source inside this project.";
      return false;
    }
    candidate["hud"] =
        std::filesystem::relative(absolute, sceneDocument_.path().parent_path())
            .generic_string();
  }
  return mutateAndRebuild(
      [&](EditorSceneDocument &document, std::string &issue) {
        return document.setHud(
            candidate.contains("hud")
                ? std::optional<nlohmann::json>(candidate["hud"])
                : std::nullopt,
            issue);
      },
      error);
}

EditorHudDocument *EditorWorkspace::activeHudDocument() {
  if (usesOpenedHudDocument_ && openedHudDocument_)
    return &*openedHudDocument_;
  return hudDocument_ ? &*hudDocument_ : nullptr;
}

const runtime::ui::UiDocument &EditorWorkspace::displayedHud() const {
  if (const EditorHudDocument *hud = hudDocument();
      activeDocument_ == EditorWorkspaceDocument::Hud && hud != nullptr)
    return hud->preview();
  return project_->world.ui;
}

bool EditorWorkspace::activateSceneDocument(std::string &error) {
  if (activeDocument_ == EditorWorkspaceDocument::TerrainAsset) {
    if (!terrainReady(error))
      return false;
    if (parkedSceneWorld_ &&
        !applyTerrainAssetPreview(*parkedSceneWorld_, error,
                                  &*parkedTerrainPrefabs_))
      return false;
    leaveTerrainAssetDocument();
  }
  activeDocument_ = EditorWorkspaceDocument::Scene;
  usesOpenedHudDocument_ = false;
  clearHudSelection();
  if (selectedEntityIds_.empty() && !project_->world.entities.empty())
    selectEntity(project_->world.entities.front().id);
  syncTerrainAuthoring();
  return true;
}

void EditorWorkspace::activateHudDocument() {
  if (!hasHudDocument())
    return;
  activeDocument_ = EditorWorkspaceDocument::Hud;
  usesOpenedHudDocument_ = openedHudDocument_.has_value();
  selectedEntityIds_.clear();
  selectedIsoGridCell_.reset();
  const EditorHudDocument *hud = hudDocument();
  if (hud != nullptr &&
      (selectedHudNodeId_.empty() || selectedHudNode() == nullptr))
    selectHudNode(hud->preview().nodes.empty()
                      ? std::string{}
                      : hud->preview().nodes.front().id);
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
  clearHudSelection();
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
  if (!project_)
    return;
  EditorHudDocument *active = activeHudDocument();
  if (!active)
    return;
  if (hudDocument_ && active == &*hudDocument_) {
    project_->world.ui = active->preview();
    project_->world.hudCanvasSize = project_->world.ui.canvasSize;
  }
  if (!selectedHudNodeId_.empty() && selectedHudNode() == nullptr)
    clearHudSelection();
}

void EditorWorkspace::selectEntity(std::string id) {
  endHudContinuousEdit();
  ++selectionRevision_;
  if (project_ && !sceneDocument_.entity(id))
    if (auto owner = editorPlacementOwner(project_->world, id); !owner.empty())
      id = std::move(owner);
  if (activeDocument_ != EditorWorkspaceDocument::TerrainAsset)
    activeDocument_ = EditorWorkspaceDocument::Scene;
  selectedIsoGridCell_.reset();
  clearHudSelection();
  selectedEntityIds_.clear();
  if (!id.empty())
    selectedEntityIds_.push_back(std::move(id));
  syncTerrainAuthoring();
}

void EditorWorkspace::selectHudNode(std::string id) {
  endHudContinuousEdit();
  ++selectionRevision_;
  activeDocument_ = EditorWorkspaceDocument::Hud;
  selectedIsoGridCell_.reset();
  selectedEntityIds_.clear();
  selectedHudNodeIds_.clear();
  selectedHudNodeId_ = std::move(id);
  if (!selectedHudNodeId_.empty())
    selectedHudNodeIds_.push_back(selectedHudNodeId_);
  syncTerrainAuthoring();
}

void EditorWorkspace::toggleHudNodeSelection(std::string id) {
  endHudContinuousEdit();
  ++selectionRevision_;
  activeDocument_ = EditorWorkspaceDocument::Hud;
  selectedIsoGridCell_.reset();
  selectedEntityIds_.clear();
  const auto found = std::ranges::find(selectedHudNodeIds_, id);
  if (found == selectedHudNodeIds_.end())
    selectedHudNodeIds_.push_back(std::move(id));
  else
    selectedHudNodeIds_.erase(found);
  selectedHudNodeId_ =
      selectedHudNodeIds_.empty() ? std::string{} : selectedHudNodeIds_.back();
  syncTerrainAuthoring();
}

bool EditorWorkspace::isHudNodeSelected(std::string_view id) const {
  return std::ranges::find(selectedHudNodeIds_, id) !=
         selectedHudNodeIds_.end();
}

void EditorWorkspace::selectIsoGridCell(EditorIsoGridCell cell) {
  selectEntity(cell.gridEntityId);
  selectedIsoGridCell_ = std::move(cell);
}

void EditorWorkspace::toggleEntitySelection(std::string id) {
  ++selectionRevision_;
  if (project_ && !sceneDocument_.entity(id))
    if (auto owner = editorPlacementOwner(project_->world, id); !owner.empty())
      id = std::move(owner);
  selectedIsoGridCell_.reset();
  const auto found = std::ranges::find(selectedEntityIds_, id);
  if (found == selectedEntityIds_.end())
    selectedEntityIds_.push_back(std::move(id));
  else
    selectedEntityIds_.erase(found);
  syncTerrainAuthoring();
}

bool EditorWorkspace::isEntitySelected(const std::string_view id) const {
  return std::ranges::find(selectedEntityIds_, id) != selectedEntityIds_.end();
}

} // namespace demi::editor
