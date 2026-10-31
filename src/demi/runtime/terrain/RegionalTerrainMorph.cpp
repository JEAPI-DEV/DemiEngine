#include "demi/runtime/terrain/RegionalTerrainMorph.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace demi::runtime {
RegionalTerrainMorph::RegionalTerrainMorph(
    World &world, std::vector<RegionalTerrainMorphPatch> patches)
    : patches_(std::move(patches)) {
  for (const auto &patch : patches_) {
    auto *owner = findEntity(world, patch.owner);
    if (!owner || !patch.field || !patch.field->morphFromHeights) {
      throw std::invalid_argument(
          "Terrain morph requires a live owner and endpoints");
    }
    owner->updateComponent<Terrain3DComponent>(
        [&](auto &terrain) { terrain.generated = patch.field; });
    owner->setComponent(TerrainVisualMorph{0});
    std::shared_ptr<PreparedTerrainChunkLod> descriptor;
    for (const auto &id : patch.surfaces) {
      auto *entity = findEntity(world, id);
      const auto *lod =
          entity ? entity->component<TerrainRenderLod>() : nullptr;
      if (!lod) {
        continue;
      }
      if (!descriptor) {
        descriptor = std::make_shared<PreparedTerrainChunkLod>(*lod->chunk);
        descriptor->field = patch.field;
        descriptor->geometryIdentity =
            std::make_shared<TerrainLodGeometryIdentity>();
        // Culling must contain the whole transition, including disappearing
        // peaks.
        for (std::size_t sample = 0; sample < patch.field->heights.size();
             ++sample) {
          for (const float height :
               {patch.field->heights.at(sample),
                patch.field->morphFromHeights->at(sample)}) {
            descriptor->boundsMin.y = std::min(descriptor->boundsMin.y, height);
            descriptor->boundsMax.y = std::max(descriptor->boundsMax.y, height);
          }
        }
      }
      entity->updateComponent<TerrainRenderLod>(
          [&](auto &value) { value.chunk = descriptor; });
    }
  }
  apply(world, 0);
}
void RegionalTerrainMorph::apply(World &world, float amount) {
  ProfileScope profile("Terrain.region.morph");
  for (const auto &patch : patches_) {
    if (auto *owner = findEntity(world, patch.owner)) {
      owner->updateComponent<TerrainVisualMorph>(
          [&](auto &value) { value.amount = amount; });
    }
    const auto &field = *patch.field;
    for (const auto &id : patch.surfaces) {
      auto *entity = findEntity(world, id);
      if (!entity || !entity->hasComponent<MeshRendererComponent>()) {
        continue;
      }
      // Keep the native fallback exact while its GPU field prepares, and when
      // GPU terrain is disabled. Collision arrays are deliberately not mutated.
      entity->updateComponent<MeshRendererComponent>([&](auto &mesh) {
        for (auto &vertex : mesh.vertices) {
          const int x = std::clamp(
              int(std::lround(vertex.x / field.size.x * field.cellsX)), 0,
              field.cellsX);
          const int z = std::clamp(
              int(std::lround(vertex.z / field.size.y * field.cellsZ)), 0,
              field.cellsZ);
          const auto sample = field.index(x, z);
          vertex.y = std::lerp(field.morphFromHeights->at(sample),
                               field.heights.at(sample), amount);
        }
        mesh.markGeometryChanged();
      });
      const auto revision =
          entity->component<MeshRendererComponent>()->revision;
      entity->updateComponent<TerrainRenderLod>(
          [&](auto &lod) { lod.fullGeometryRevision = revision; });
    }
  }
}
bool RegionalTerrainMorph::advance(World &world, float deltaSeconds) {
  if (!std::isfinite(deltaSeconds) || deltaSeconds < 0) {
    throw std::invalid_argument(
        "Terrain morph delta must be finite and nonnegative");
  }
  constexpr float DurationSeconds = 0.25F;
  elapsed_ = std::min(elapsed_ + deltaSeconds, DurationSeconds);
  const float linear = elapsed_ / DurationSeconds;
  const float amount = linear * linear * (3.F - 2.F * linear);
  apply(world, amount);
  return elapsed_ >= DurationSeconds;
}
} // namespace demi::runtime
