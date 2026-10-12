#include "demi/runtime/render/bgfx3d/TerrainCameraDemand3D.h"

#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainLodResidency.h"

#include <algorithm>
#include <functional>

namespace demi::runtime::render {

std::string TerrainCameraDemand3D::cameraKey(const BgfxCameraFrame3D &camera) {
  return camera.cameraId.empty() ? "view:" + std::to_string(camera.viewId)
                                 : camera.cameraId;
}

std::size_t
TerrainCameraDemand3D::SourceHash::operator()(const SourceKey &key) const {
  const auto owner = std::hash<std::string>{}(key.owner);
  const auto chunk = std::hash<const PreparedTerrainChunkLod *>{}(key.chunk);
  return owner ^ (chunk + 0x9e3779b9U + (owner << 6U) + (owner >> 2U));
}

void TerrainCameraDemand3D::beginPresentation() {
  for (auto &[id, camera] : cameras_) {
    camera.visited = false;
  }
}

void TerrainCameraDemand3D::beginCamera(
    const World &world, const BgfxCameraFrame3D &camera,
    TerrainLodResidency &residency,
    const std::function<void(const std::shared_ptr<const HeightField> &)>
        &gpuTouch) {
  auto &demand = cameras_[cameraKey(camera)];
  const bool worldChanged = demand.world != &world ||
                            demand.worldId != world.id ||
                            demand.sceneId != world.activeSceneId;
  demand.visited = true;
  if (worldChanged || camera.updateContent) {
    demand.sources.clear();
  }
  demand.world = &world;
  demand.worldId = world.id;
  demand.sceneId = world.activeSceneId;
  if (camera.updateContent) {
    return;
  }
  for (auto source = demand.sources.begin(); source != demand.sources.end();) {
    const auto descriptor = source->second.descriptor.lock();
    const auto *surface =
        lookup_.find(world.entities, source->second.surfaceId);
    const auto *current = surface && surface->enabled
                              ? surface->component<TerrainRenderLod>()
                              : nullptr;
    if (!descriptor || !current || !current->chunk ||
        current->owner != source->first.owner ||
        current->chunk->geometryIdentity != descriptor->geometryIdentity) {
      source = demand.sources.erase(source);
      continue;
    }
    // Appearance-only replacement can borrow the same geometry with updated
    // bindings; a geometric/source identity replacement is never replayed.
    source->second.descriptor = current->chunk;
    if (source->second.gpuField) {
      const auto *owner = lookup_.find(world.entities, current->owner);
      const auto *terrain =
          owner ? owner->component<Terrain3DComponent>() : nullptr;
      if (gpuTouch) {
        gpuTouch(terrain && terrain->generated ? terrain->generated
                                               : current->chunk->field);
      }
    } else {
      (void)residency.request(current->chunk, source->second.level);
    }
    ++source;
  }
}

void TerrainCameraDemand3D::record(
    const BgfxCameraFrame3D &camera, const std::string &surfaceId,
    const std::string &owner,
    const std::shared_ptr<const PreparedTerrainChunkLod> &descriptor,
    std::size_t level, bool gpuField) {
  if (level == 0 && !gpuField) {
    return;
  }
  auto &sources = cameras_[cameraKey(camera)].sources;
  sources[SourceKey{owner, descriptor.get()}] = {surfaceId, descriptor, level,
                                                 gpuField};
}

void TerrainCameraDemand3D::endPresentation() {
  std::erase_if(cameras_, [](const auto &item) {
    return !item.second.visited || item.second.sources.empty();
  });
}

void TerrainCameraDemand3D::clear() {
  cameras_.clear();
  lookup_.clear();
}

} // namespace demi::runtime::render
