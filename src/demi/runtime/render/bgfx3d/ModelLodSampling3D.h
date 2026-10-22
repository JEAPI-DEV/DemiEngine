#pragma once

#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Dentable3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SurfaceRelief3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>

namespace demi::runtime::render {

inline std::uint16_t effectiveModelLodPixelHeight3D(
    const BgfxCameraFrame3D &camera,
    std::optional<std::uint16_t> authoredTargetHeight = std::nullopt) {
  if (authoredTargetHeight) {
    return *authoredTargetHeight;
  }
  if (camera.viewportHeight == 0 || !std::isfinite(camera.camera.renderScale)) {
    return 0;
  }
  const float height =
      camera.viewportHeight * std::clamp(camera.camera.renderScale, 0.25F, 2.F);
  return static_cast<std::uint16_t>(
      std::clamp(std::lround(height), 1L, static_cast<long>(UINT16_MAX)));
}

// This equates sampling footprints, not geometric or alpha-coverage errors.
// Authored LOD distance thresholds carry no maximum-deviation guarantee.
inline float shadowModelLodDistanceFloor3D(float worldUnitsPerShadowTexel,
                                           const BgfxCameraFrame3D &mainCamera,
                                           std::uint16_t effectivePixelHeight) {
  if (!mainCamera.camera.perspective || effectivePixelHeight == 0 ||
      !std::isfinite(worldUnitsPerShadowTexel) ||
      worldUnitsPerShadowTexel <= 0 || !std::isfinite(mainCamera.camera.fov) ||
      mainCamera.camera.fov <= 0 || mainCamera.camera.fov >= 180) {
    return 0;
  }
  const float tangent =
      std::tan(mainCamera.camera.fov * std::numbers::pi_v<float> / 360.F);
  const float floor =
      worldUnitsPerShadowTexel * effectivePixelHeight / (2.F * tangent);
  return std::isfinite(floor) ? floor : 0;
}

inline float staticShadowModelLodFloor3D(const VisibleMesh3D &draw,
                                         const BgfxCameraFrame3D &frame,
                                         bool shadowPass) {
  if (!shadowPass || !(frame.shadowModelLodDistanceFloor > 0)) {
    return 0;
  }
  const auto *mesh = draw.preparedSource
                         ? draw.preparedSource->mesh
                         : draw.entity->component<MeshRendererComponent>();
  if (mesh == nullptr || mesh->model.empty() || !mesh->vertices.empty()) {
    return 0;
  }
  if (const auto *source = draw.preparedSource) {
    if (source->animation || source->dentable || source->relief ||
        !source->dents.empty()) {
      return 0;
    }
  } else if (draw.entity->hasComponent<AnimationPlayer3DComponent>() ||
             draw.entity->hasComponent<Dentable3DComponent>() ||
             draw.entity->hasComponent<SurfaceRelief3DComponent>() ||
             !entityMeshDents3D(*draw.entity).empty()) {
    return 0;
  }
  return frame.shadowModelLodDistanceFloor;
}

struct ModelLodGeometrySelection3D {
  const std::string *model = nullptr;
  bool isCulled = false;
  int level = 0;
};

inline ModelLodGeometrySelection3D
selectModelLodGeometry3D(const MeshRendererComponent &mesh, Vec3 position,
                         Vec3 camera, bool preserveHigh,
                         float shadowDistanceFloor = 0) {
  const float x = position.x - camera.x;
  const float y = position.y - camera.y;
  const float z = position.z - camera.z;
  const float actualSquaredDistance = x * x + y * y + z * z;
  // Sampling resolution must never change authored distance culling.
  if (mesh.cullDistance > 0 &&
      actualSquaredDistance >= mesh.cullDistance * mesh.cullDistance) {
    return {.model = &mesh.model, .isCulled = true};
  }
  ModelLodGeometrySelection3D selected{.model = &mesh.model};
  if (preserveHigh) {
    return selected;
  }
  float detailSquaredDistance = actualSquaredDistance;
  if (std::isfinite(shadowDistanceFloor) && shadowDistanceFloor > 0) {
    detailSquaredDistance = std::max(detailSquaredDistance,
                                     shadowDistanceFloor * shadowDistanceFloor);
  }
  const auto reached = [&](float threshold) {
    return threshold > 0 && detailSquaredDistance >= threshold * threshold;
  };
  if (!mesh.lowLodModel.empty() && reached(mesh.lowLodDistance)) {
    selected = {.model = &mesh.lowLodModel, .level = 2};
  } else if (!mesh.mediumLodModel.empty() && reached(mesh.mediumLodDistance)) {
    selected = {.model = &mesh.mediumLodModel, .level = 1};
  }
  return selected;
}

} // namespace demi::runtime::render
