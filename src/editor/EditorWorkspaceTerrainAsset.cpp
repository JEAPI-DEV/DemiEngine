#include "editor/EditorWorkspace.h"

#include "editor/EditorTerrainRuntime.h"

#include "demi/assets/TerrainAsset.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainWorldBatch.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace demi::editor {
namespace {

const EditorAssetRecord *terrainRecord(const EditorWorkspace &workspace,
                                       const std::filesystem::path &path) {
  const auto &index = workspace.assetIndex();
  const EditorAssetRecord *record = index.findBySource(path);
  if (!record)
    record = index.findByManifest(path);
  return record && record->manifest.type == "Terrain" ? record : nullptr;
}

constexpr std::string_view PreviewOwner = "terrain";

EditorTerrainUpdate replacementSurface(EditorTerrainSurfacePtr before,
                                       EditorTerrainSurfacePtr after) {
  auto patch = std::make_shared<runtime::TerrainPatch>();
  patch->fullBefore = before ? before->heightField() : nullptr;
  patch->fullAfter = after->heightField();
  patch->invalidation.fullGeneration = true;
  patch->invalidation.layoutChanged = true;
  return {std::move(after), std::move(patch)};
}

bool installSharedTerrain(runtime::World &world, std::string_view assetId,
                          const nlohmann::json &recipe,
                          const EditorTerrainUpdate &update, std::string &error,
                          runtime::RuntimePrefabService *prefabs) {
  if (!update.surface || !update.surface->heightField() || !update.patch) {
    error = "Terrain publication requires a native surface and patch.";
    return false;
  }
  runtime::TerrainUpdate native;
  native.field = update.surface->heightField();
  native.patch = update.patch;
  native.invalidation = update.patch->invalidation;
  return runtime::updateTerrainAssetWorld(world, assetId, recipe, native, error,
                                          prefabs);
}
} // namespace

bool EditorWorkspace::terrainEditingAsset() const {
  if (!terrainAssetBinding_ || !terrainAssetDocument_ || !project_ ||
      (!terrainAuthoringPinned() &&
       activeDocument_ != EditorWorkspaceDocument::Scene &&
       activeDocument_ != EditorWorkspaceDocument::TerrainAsset))
    return false;
  const auto *entity =
      runtime::findEntity(project_->world, terrainAuthoring_->entityId());
  const auto *terrain =
      entity ? entity->component<runtime::Terrain3DComponent>() : nullptr;
  return terrain && terrain->asset == terrainAssetDocument_->id();
}

bool EditorWorkspace::applyTerrainAssetChanges(std::string &error) {
  return saveTerrainAsset(error);
}

bool EditorWorkspace::bindTerrainAsset(std::string_view assetId,
                                       std::string &error) {
  if (terrainAssetDocument_ && terrainAssetDocument_->id() == assetId)
    return true;
  if (terrainAssetDocument_ &&
      (terrainAssetDocument_->isDirty() || terrainAssetCachePending_ ||
       terrainAssetDraft_)) {
    error = "Apply or undo changes and generate or discard the draft for "
            "Terrain asset '" +
            terrainAssetDocument_->id() +
            "' before editing another terrain asset.";
    return false;
  }
  const auto *manifest =
      findAsset(assetIndex_.registry(), std::string(assetId));
  if (!manifest || manifest->type != "Terrain") {
    error = "The selected Terrain asset cannot be resolved: " +
            std::string(assetId);
    return false;
  }
  EditorTerrainAssetDocument candidate;
  if (!candidate.open(*manifest, error))
    return false;
  terrainAssetDocument_ = std::move(candidate);
  terrainAssetSurface_.reset();
  terrainAssetDraft_.reset();
  terrainAssetCachePending_ = false;
  lastEditTerrainAsset_ = false;
  lastTerrainAssetPath_ = terrainAssetDocument_->path();
  return true;
}

bool EditorWorkspace::applyTerrainAssetPreview(
    runtime::World &world, std::string &error,
    runtime::RuntimePrefabService *prefabs) {
  if (!terrainAssetDocument_ || !terrainAssetSurface_)
    return true;
  for (const auto &entity : world.entities) {
    const auto *terrain = entity.component<runtime::Terrain3DComponent>();
    if (!terrain || terrain->asset != terrainAssetDocument_->id())
      continue;
    return installSharedTerrain(
        world, terrainAssetDocument_->id(), terrainAssetDocument_->recipe(),
        replacementSurface(currentEditorTerrain(world, entity.id),
                           terrainAssetSurface_),
        error, prefabs ? prefabs : &terrainPreviewPrefabs_);
  }
  return true;
}

bool EditorWorkspace::openTerrainAssetDocument(
    const std::filesystem::path &path, std::string &error) {
  if (!project_) {
    error = "Open a project before editing terrain assets.";
    return false;
  }
  const EditorAssetRecord *record = terrainRecord(*this, path);
  if (!record) {
    error = "This source is not an imported Terrain asset: " + path.string();
    return false;
  }
  if (activeDocument_ == EditorWorkspaceDocument::TerrainAsset &&
      terrainAssetDocument_ &&
      terrainAssetDocument_->path() == record->manifest.sourcePath)
    return true;
  if (terrainAssetBinding_ && terrainAuthoring_->hasDraftChanges())
    terrainAssetDraft_ = terrainAuthoring_->draft();
  if (terrainAssetDocument_ &&
      terrainAssetDocument_->id() != record->manifest.id &&
      (terrainAssetDocument_->isDirty() || terrainAssetCachePending_ ||
       terrainAssetDraft_)) {
    error = "Save or undo the current terrain asset before opening another.";
    return false;
  }
  if (!terrainReady(error))
    return false;
  EditorTerrainAssetDocument candidate;
  if (terrainAssetDocument_ &&
      terrainAssetDocument_->id() == record->manifest.id)
    candidate = *terrainAssetDocument_;
  else if (!candidate.open(record->manifest, error))
    return false;
  try {
    (void)assets::prepareTerrainAsset(assetIndex_.registry(),
                                      record->manifest.id);
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }

  // Use the real scene loader on a transient document, so the preview consumes
  // exactly the same prepared asset and terrain geometry as scene instances.
  auto previewProject = project_->project;
  const std::string previewId = "scene://editor-terrain-asset";
  previewProject.scenes = {
      {.id = previewId,
       .path = previewProject.projectDirectory /
               "scenes/__terrain_asset_preview.scene.json"}};
  const nlohmann::json owner{{"id", std::string(PreviewOwner)},
                             {"components",
                              {{"Transform3D", nlohmann::json::object()},
                               {"Terrain3D", {{"asset", candidate.id()}}}}}};
  const nlohmann::json preview{
      {"format_version", 1},
      {"id", previewId},
      {"name", candidate.json().value("name", std::string("Terrain asset"))},
      {"entities", nlohmann::json::array({owner})}};
  runtime::RuntimePrefabService previewPrefabs;
  previewPrefabs.configure(project_->project.projectDirectory);
  auto world = runtime::loadSceneDocument(previewProject, previewId, preview,
                                          error, false, &previewPrefabs);
  if (!world)
    return false;
  if (terrainAssetDocument_ && terrainAssetDocument_->id() == candidate.id() &&
      !applyTerrainAssetPreview(*world, error, &previewPrefabs))
    return false;

  if (!restoreTerrainPreview(error))
    return false;
  terrainAuthoring_->unbind();
  if (!parkedSceneWorld_ && !sceneDocument_.path().empty()) {
    parkedSceneWorld_.emplace(std::move(project_->world));
    parkedTerrainPrefabs_.emplace(std::move(terrainPreviewPrefabs_));
    parkedSceneView_ = sceneView_;
    parkedSceneViewDimension_ = viewDimension_;
    parkedSceneSelection_ = selectedEntityIds_;
  }
  terrainPreviewPrefabs_ = std::move(previewPrefabs);
  project_->world = std::move(*world);
  terrainAssetDocument_ = std::move(candidate);
  terrainAssetSurface_ = currentEditorTerrain(project_->world, PreviewOwner);
  terrainAssetBinding_ = true;
  configureTerrainInputResolver();
  lastTerrainAssetPath_ = terrainAssetDocument_->path();
  activeDocument_ = EditorWorkspaceDocument::TerrainAsset;
  selectedEntityIds_ = {std::string(PreviewOwner)};
  selectedHudNodeId_.clear();
  selectedIsoGridCell_.reset();
  viewportTool_.cancelDrag();
  viewDimension_ = EditorSceneViewDimension::ThreeDimensional;
  sceneView_.reset(project_->world);
  (void)sceneView_.frameScene(project_->world);
  syncTerrainAuthoring();
  refreshDiagnostics();
  return true;
}

void EditorWorkspace::leaveTerrainAssetDocument() {
  if (terrainAssetBinding_ && terrainAuthoring_->hasDraftChanges())
    terrainAssetDraft_ = terrainAuthoring_->draft();
  terrainAuthoring_->unbind();
  terrainPreview_.reset();
  terrainPreviewSurface_.reset();
  terrainAssetBinding_ = false;
  configureTerrainInputResolver();
  if (parkedSceneWorld_) {
    project_->world = std::move(*parkedSceneWorld_);
    parkedSceneWorld_.reset();
    terrainPreviewPrefabs_ = std::move(*parkedTerrainPrefabs_);
    parkedTerrainPrefabs_.reset();
    sceneView_ = parkedSceneView_;
    viewDimension_ = parkedSceneViewDimension_;
    selectedEntityIds_ = std::move(parkedSceneSelection_);
  }
  activeDocument_ = EditorWorkspaceDocument::Scene;
  if (selectedEntityIds_.empty() && !project_->world.entities.empty())
    selectedEntityIds_.push_back(project_->world.entities.front().id);
  refreshDiagnostics();
}

bool EditorWorkspace::placeTerrainAsset(
    const std::filesystem::path &path,
    const std::optional<runtime::Vec3> position, std::string &error) {
  if (activeDocument_ != EditorWorkspaceDocument::Scene || editingPrefab_) {
    error = "Open a 3D scene before placing a terrain asset.";
    return false;
  }
  const EditorAssetRecord *record = terrainRecord(*this, path);
  if (!record) {
    error = "Choose an imported Terrain asset.";
    return false;
  }
  try {
    (void)assets::prepareTerrainAsset(assetIndex_.registry(),
                                      record->manifest.id);
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }
  return mutateAndRebuild(
      [id = record->manifest.id, position](EditorSceneDocument &scene,
                                           std::string &issue) {
        return scene.createTerrainAssetEntity(id, position, issue);
      },
      error);
}

bool EditorWorkspace::assignTerrainAsset(const std::string_view entityId,
                                         const std::string_view assetId,
                                         std::string &error) {
  if (activeDocument_ != EditorWorkspaceDocument::Scene) {
    error = "Open a scene to assign a terrain asset.";
    return false;
  }
  const auto *component = sceneDocument_.component(entityId, "Terrain3D");
  if (!component) {
    error = "The selected entity has no Terrain 3D component.";
    return false;
  }
  const AssetManifest *manifest =
      findAsset(assetIndex_.registry(), std::string(assetId));
  if (!manifest || manifest->type != "Terrain") {
    error = "Choose an imported Terrain asset.";
    return false;
  }
  try {
    (void)assets::prepareTerrainAsset(assetIndex_.registry(), assetId);
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }
  return mutateAndRebuild(
      [entity = std::string(entityId), asset = std::string(assetId)](
          EditorSceneDocument &scene, std::string &issue) {
        return scene.setTerrainAsset(entity, asset, issue);
      },
      error);
}

std::filesystem::path EditorWorkspace::terrainAuthoringDocumentPath() const {
  if (terrainEditingAsset())
    return terrainAssetDocument_->path();
  return sceneDocument_.path();
}

bool EditorWorkspace::pollTerrainAssetAuthoring(EditorTerrainCommit commit,
                                                std::string &error) {
  if (!terrainAssetDocument_) {
    error = "The edited Terrain asset was closed during generation.";
    return false;
  }
  const auto &source = *terrainAssetDocument_;
  const auto *owner = runtime::findEntity(project_->world, commit.entityId);
  const auto *terrain =
      owner ? owner->component<runtime::Terrain3DComponent>() : nullptr;
  if (commit.document != source.path().string() || !terrain ||
      terrain->asset != source.id() || commit.before != source.recipe()) {
    error = "Terrain asset changed while generating; the result was discarded.";
    std::string ignored;
    (void)restoreTerrainPreview(ignored);
    return false;
  }
  if (!commit.final) {
    const auto original =
        terrainPreview_
            ? terrainPreviewSurface_
            : currentEditorTerrain(project_->world, commit.entityId);
    if (terrainPreview_)
      commit.patch =
          mergeEditorTerrainPatches(terrainPreview_->patch, commit.patch);
    if (!installSharedTerrain(project_->world, source.id(), commit.recipe,
                              {commit.surface, commit.publicationPatch
                                                   ? commit.publicationPatch
                                                   : commit.patch},
                              error, &terrainPreviewPrefabs_))
      return false;
    terrainPreview_ = std::move(commit);
    terrainPreviewSurface_ = original;
    return true;
  }

  EditorTerrainAssetDocument before = *terrainAssetDocument_;
  if (!terrainAssetDocument_->setRecipe(commit.recipe, error))
    return false;
  const bool alreadyPublished =
      terrain->generated == commit.surface->heightField() &&
      terrain->recipe == commit.recipe;
  if (!alreadyPublished &&
      !installSharedTerrain(project_->world, source.id(), commit.recipe,
                            {commit.surface, commit.publicationPatch
                                                 ? commit.publicationPatch
                                                 : commit.patch},
                            error, &terrainPreviewPrefabs_)) {
    *terrainAssetDocument_ = std::move(before);
    return false;
  }
  publishEditorTerrain(commit.recipe, commit.surface);
  terrainAssetSurface_ = commit.surface;
  lastEditTerrainAsset_ = true;
  terrainAssetDraft_.reset();
  terrainPreview_.reset();
  terrainPreviewSurface_.reset();
  syncTerrainAuthoring();
  return true;
}

bool EditorWorkspace::refreshTerrainAssetPreview(const nlohmann::json &recipe,
                                                 std::string &error) {
  const auto owner =
      std::ranges::find_if(project_->world.entities, [&](const auto &entity) {
        const auto *terrain =
            entity.template component<runtime::Terrain3DComponent>();
        return terrain && terrainAssetDocument_ &&
               terrain->asset == terrainAssetDocument_->id();
      });
  const auto *terrain = owner != project_->world.entities.end()
                            ? owner->component<runtime::Terrain3DComponent>()
                            : nullptr;
  if (!terrain) {
    error = "Terrain asset preview owner is missing.";
    return false;
  }
  try {
    auto update = updateEditorTerrainWithInputs(
        terrain->recipe, recipe,
        currentEditorTerrain(project_->world, owner->id),
        assets::resolveTerrainAssetGenerationInputs(
            assetIndex_.registry(), runtime::TerrainRecipe::parse(recipe)),
        {}, {}, error);
    if (!update)
      return false;
    if (!installSharedTerrain(project_->world, terrainAssetDocument_->id(),
                              recipe, *update, error, &terrainPreviewPrefabs_))
      return false;
    publishEditorTerrain(recipe, update->surface);
    terrainAssetSurface_ = update->surface;
    return true;
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }
}

bool EditorWorkspace::terrainAssetHistory(bool forward, std::string &error) {
  if (!terrainAssetDocument_) {
    error = "There is no open Terrain asset.";
    return false;
  }
  EditorTerrainAssetDocument candidate = *terrainAssetDocument_;
  if (!(forward ? candidate.redo(error) : candidate.undo(error)))
    return false;
  if (!refreshTerrainAssetPreview(candidate.recipe(), error))
    return false;
  *terrainAssetDocument_ = std::move(candidate);
  lastEditTerrainAsset_ = true;
  terrainAssetDraft_.reset();
  syncTerrainAuthoring();
  return true;
}

bool EditorWorkspace::rebuildParkedSceneWorld(std::string &error) {
  if (!parkedSceneWorld_)
    return true;
  runtime::RuntimePrefabService previewPrefabs;
  previewPrefabs.configure(project_->project.projectDirectory);
  auto world =
      loadEntityPreview(sceneDocument_, editingPrefab_, error, &previewPrefabs);
  if (!world)
    return false;
  if (!applyTerrainAssetPreview(*world, error, &previewPrefabs))
    return false;
  parkedSceneWorld_ = std::move(*world);
  parkedTerrainPrefabs_ = std::move(previewPrefabs);
  return true;
}

bool EditorWorkspace::saveTerrainAsset(std::string &error) {
  if (!terrainAssetDocument_) {
    error = "There is no open Terrain asset to apply.";
    return false;
  }
  if (!terrainReady(error))
    return false;
  const auto surface = terrainAssetSurface_;
  if (!surface || !surface->heightField()) {
    error = "The Terrain asset has no generated preview to apply.";
    return false;
  }
  terrainAssetCachePending_ = true;
  if (!terrainAssetDocument_->save(error))
    return false;
  refreshAssetMetadata();
  try {
    (void)assets::storeTerrainAssetPreview(
        assetIndex_.registry(), terrainAssetDocument_->id(),
        terrainAssetDocument_->recipe(), surface->heightField());
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }
  if (!rebuildParkedSceneWorld(error))
    return false;
  terrainAssetCachePending_ = false;
  refreshDiagnostics();
  return true;
}

} // namespace demi::editor
