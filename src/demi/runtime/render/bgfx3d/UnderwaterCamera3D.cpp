#include "demi/runtime/render/bgfx3d/UnderwaterCamera3D.h"

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"

#include <algorithm>

namespace demi::runtime::render {

std::optional<UnderwaterAppearance3D>
UnderwaterCamera3D::sample(const World &world, Vec3 cameraPosition) {
  const auto sample = water_.sample(world, cameraPosition);
  if (!sample || !sample->underwater) {
    return std::nullopt;
  }
  const auto *owner = owners_.find(world.entities, sample->terrainId);
  const auto *terrain =
      owner ? owner->component<Terrain3DComponent>() : nullptr;
  const auto transform =
      owner ? resolveWorldTransform3D(world, *owner) : std::nullopt;
  if (!terrain || !terrain->generated || !terrain->generated->graphArtifacts ||
      !transform) {
    return std::nullopt;
  }
  const auto &bodies = terrain->generated->graphArtifacts->water.bodies;
  const auto body =
      std::ranges::find(bodies, sample->bodyId, &TerrainWaterBodySpec::id);
  if (body == bodies.end()) {
    return std::nullopt;
  }
  const auto localCamera = inverseTransformPoint3D(*transform, cameraPosition);
  const auto localSurface =
      inverseTransformPoint3D(*transform, sample->surface);
  // Fade through a short local band, never enable while above the queried
  // surface. Camera switching cannot inherit another camera's immersion.
  const float immersion =
      std::clamp((localSurface.y - localCamera.y) / 0.15F, 0.F, 1.F);
  return UnderwaterAppearance3D{
      .color = body->appearance.deepColor,
      .absorptionDistance = body->appearance.absorptionDistance,
      .immersion = immersion,
      .surfaceRelative = {sample->surface.x - cameraPosition.x,
                          sample->surface.y - cameraPosition.y,
                          sample->surface.z - cameraPosition.z},
      .normal = sample->normal,
      .inverseBasis = {inverseTransformVector3D(*transform, {1, 0, 0}),
                       inverseTransformVector3D(*transform, {0, 1, 0}),
                       inverseTransformVector3D(*transform, {0, 0, 1})}};
}

} // namespace demi::runtime::render
