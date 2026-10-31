#include "demi/runtime/render/BgfxRenderer3D.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/bgfx3d/SceneLighting3D.h"
#include "demi/runtime/terrain/RegionalTerrainPresentation.h"

#include <algorithm>
#include <limits>

namespace demi::runtime::render {

void BgfxRenderer3D::configureTerrainPresentation(
    const World &world, const BgfxCameraFrame3D &frame,
    const SceneLighting3D &sceneLighting) {
  // A loading HUD with no terrain must not reset the incoming landscape's
  // array-quality settings or invalidate its in-flight preparation jobs.
  if (world.terrainOwners.empty() &&
      !std::ranges::any_of(world.entities, [](const auto &entity) {
        return entity.template hasComponent<RegionalTerrainPresentation>();
      })) {
    return;
  }
  terrainLodResidency_.setSettings(
      {.maximumResidentBytes = sceneLighting.terrainLodCacheBytes,
       .maximumPendingJobs = sceneLighting.terrainLodPrepareJobs,
       .maximumJobsPerPresentation = sceneLighting.terrainLodPrepareJobs});
  const auto topologyBytes = terrainPatches_.topologyBytes();
  const auto gpuFieldBudget =
      sceneLighting.terrainGpuCacheBytes > topologyBytes
          ? sceneLighting.terrainGpuCacheBytes - topologyBytes
          : 0;
  terrainGpuFields_.setBudgets(gpuFieldBudget,
                               sceneLighting.terrainLodCacheBytes,
                               sceneLighting.terrainLodPrepareJobs);
  terrainMaterials_.setQuality(
      sceneLighting.terrainTextureMaxSize,
      sceneLighting.terrainTextureCacheBytes,
      sceneLighting.terrainDetailDistance,
      std::max(sceneLighting.terrainLodCacheBytes,
               sceneLighting.terrainTextureCacheBytes * 2),
      sceneLighting.terrainLodPrepareJobs);
  prepareRegionalTerrain(world, frame);
  terrainCameraDemand_.beginCamera(
      world, frame, terrainLodResidency_,
      [this](const auto &field) { terrainGpuFields_.touch(field); });
}

void BgfxRenderer3D::prepareRegionalTerrain(
    const World &world, const BgfxCameraFrame3D &frame) {
  ProfileScope preparation("Renderer3D.prepare_regional_cover");
  if (!regionalTerrainLease_) {
    regionalTerrainLease_ = std::make_shared<int>(0);
  }
  for (const auto &owner : world.entities) {
    if (!owner.enabled) {
      continue;
    }
    const auto *presentation = owner.component<RegionalTerrainPresentation>();
    if (!presentation || !presentation->render) {
      continue;
    }
    auto &state = *presentation->render;
    // Every camera in this presentation contributes to one readiness result.
    // A later camera cannot acknowledge work still pending for an earlier one.
    if (state.renderer.lock() != regionalTerrainLease_ ||
        state.presentation != regionalTerrainPresentation_) {
      state.prepared = true;
      state.presentation = regionalTerrainPresentation_;
      state.renderer = regionalTerrainLease_;
    }
    const auto transform = resolveWorldTransform3D(world, owner);
    if (!transform) {
      continue;
    }
    bool preparing = false;
    const auto request = [&](const RegionalTerrainRenderPatch &patch) {
      PreparedTerrainChunkLod bounds;
      bounds.boundsMin = patch.boundsMin;
      bounds.boundsMax = patch.boundsMax;
      if (!terrainChunkVisible3D(bounds, *transform, frame)) {
        return;
      }
      const auto field = terrainGpuFields_.request(patch.field, materials_);
      preparing = preparing || field.preparing;
      if (field.field && field.field->mapped) {
        // Prewarm the minimum tier; visible-camera demand still chooses detail.
        const auto arrays = terrainMaterials_.request(
            *field.field, std::numeric_limits<float>::max());
        preparing = preparing || (!arrays.arrays && arrays.preparing);
      }
    };
    for (const auto &source : state.active) {
      request(source);
    }
    for (const auto &source : state.pending) {
      request(source);
    }
    // Permanent unsupported/budget failures keep the explicit CPU path usable.
    state.prepared = state.prepared && !preparing;
  }
}

bool BgfxRenderer3D::prepareScenePresentation(const World &world,
                                              const BgfxCameraFrame3D &frame,
                                              bool &complete,
                                              std::string &error) {
  ProfileScope preparation("Renderer3D.prepare_scene_presentation");
  complete = false;
  error.clear();
  beginPresentation();
  const auto lighting =
      collectSceneLighting3D(world, frame.camera.renderMask, &entityQueries_);
  if (!lighting.skyTexture.empty()) {
    bool skyReady = false;
    if (!prepareTextureVariant(lighting.skyTexture, ImageMipSemantic::Linear,
                               skyReady, error)) {
      return false;
    }
    if (!skyReady) {
      return true;
    }
  }
  configureTerrainPresentation(world, frame, lighting);
  terrainPatches_.beginView();
  terrainPatches_.prepareExtraction(
      world, frame, frame, terrainSelection_, terrainGpuFields_, materials_,
      terrainCameraDemand_, lighting.terrainGpuCacheBytes,
      bool(terrainProgram_ && directionalTerrainProgram_), &terrainMaterials_,
      lighting.terrainDetailDistance,
      bool(mappedTerrainProgram_ && directionalMappedTerrainProgram_));
  const auto &statistics = terrainPatches_.statistics();
  if (statistics.fallbackPatches == 0 && statistics.fallbackReason.empty()) {
    complete = true;
    return true;
  }
  if (terrainGpuFields_.hasPendingWork() ||
      terrainMaterials_.hasPendingWork()) {
    return true;
  }
  // Unsupported GPU layers retain the ordinary mesh capability path. Prepare
  // that path one new chunk upload at a time, using its normal source/cache.
  auto visibility = extractVisibleMeshes3D(
      world, frame, &extractionJobs_, &terrainSelection_, &frame,
      &terrainLodResidency_, &terrainCameraDemand_, &terrainPatches_,
      &materials_,
      [this](std::string_view model) -> std::optional<MeshBounds3D> {
        const auto found = modelMeshes_.find(std::string(model));
        return found == modelMeshes_.end() ? std::nullopt
                                           : found->second->bounds;
      },
      &instancePreparation_);
  const auto uploadsBefore = terrainMeshes_.uploads();
  for (std::size_t index = 0; index < visibility.meshes.size();) {
    const auto &draw = visibility.meshes[index];
    if (!draw.terrainSurface) {
      ++index;
      continue;
    }
    if (terrainMeshes_.uploads() != uploadsBefore) {
      return true;
    }
    std::size_t end = index + 1;
    while (end < visibility.meshes.size() &&
           visibility.meshes[end].entity == draw.entity &&
           visibility.meshes[end].terrainLevel == draw.terrainLevel) {
      ++end;
    }
    SceneVisibility3D chunk;
    chunk.meshes.assign(visibility.meshes.begin() + index,
                        visibility.meshes.begin() + end);
    if (!prepareTerrainMeshes(chunk, error)) {
      return false;
    }
    index = end;
  }
  complete = !terrainLodResidency_.hasPendingWork();
  return true;
}

void BgfxRenderer3D::cancelScenePresentationPreparation() {
  cancelTextureVariantPreparation();
  // Owner profiles borrow world entities. Drop them before a cancelled prepared
  // World can be freed; immutable field/image worker inputs retain ownership.
  terrainPatches_.clear();
}

} // namespace demi::runtime::render
