#include "demi/runtime/render/bgfx3d/SceneLighting3D.h"

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/3dcomponents/DirectionalLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Environment3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/PointLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SpotLightComponent.h"

#include <cmath>
#include <numbers>

namespace demi::runtime::render {
namespace {

bool matchesMask(const std::string_view cameraMask,
                 const std::string_view lightMask) {
  return cameraMask.empty() || lightMask.empty() || cameraMask == lightMask;
}

} // namespace

SceneLighting3D collectSceneLighting3D(const World &world,
                                       const std::string_view renderMask) {
  SceneLighting3D lighting;
  bool hasAuthoredLighting = false;
  bool hasEnvironment = false;
  std::size_t pointCount = 0;
  std::size_t spotCount = 0;
  for (const Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    if (const auto *environment = entity.component<Environment3DComponent>()) {
      lighting.ambient = {
          environment->ambientColor.r * environment->ambientIntensity,
          environment->ambientColor.g * environment->ambientIntensity,
          environment->ambientColor.b * environment->ambientIntensity, 1.0F};
      hasAuthoredLighting = true;
      hasEnvironment = true;
    }
    if (const auto *directional = entity.component<DirectionalLightComponent>();
        directional != nullptr &&
        matchesMask(renderMask, directional->renderMask)) {
      lighting.direction = {directional->direction.x, directional->direction.y,
                            directional->direction.z, directional->intensity};
      lighting.directionalColor = {directional->color.r, directional->color.g,
                                   directional->color.b, 1.0F};
      hasAuthoredLighting = true;
    }
    const auto transform = resolveWorldTransform3D(world, entity);
    if (const auto *point = entity.component<PointLightComponent>();
        point != nullptr && pointCount < 4U && transform &&
        matchesMask(renderMask, point->renderMask)) {
      const std::size_t base = pointCount++ * 4U;
      lighting.pointPositionRange[base] = transform->position.x;
      lighting.pointPositionRange[base + 1U] = transform->position.y;
      lighting.pointPositionRange[base + 2U] = transform->position.z;
      lighting.pointPositionRange[base + 3U] = point->range;
      lighting.pointColorIntensity[base] = point->color.r;
      lighting.pointColorIntensity[base + 1U] = point->color.g;
      lighting.pointColorIntensity[base + 2U] = point->color.b;
      lighting.pointColorIntensity[base + 3U] = point->intensity;
      hasAuthoredLighting = true;
    }
    if (const auto *spot = entity.component<SpotLightComponent>();
        spot != nullptr && spotCount < 4U && transform &&
        matchesMask(renderMask, spot->renderMask)) {
      const std::size_t base = spotCount++ * 4U;
      const Vec3 direction = transformDirection3D(*transform, spot->direction);
      lighting.spotPositionRange[base] = transform->position.x;
      lighting.spotPositionRange[base + 1U] = transform->position.y;
      lighting.spotPositionRange[base + 2U] = transform->position.z;
      lighting.spotPositionRange[base + 3U] = spot->range;
      lighting.spotDirectionOuter[base] = direction.x;
      lighting.spotDirectionOuter[base + 1U] = direction.y;
      lighting.spotDirectionOuter[base + 2U] = direction.z;
      lighting.spotDirectionOuter[base + 3U] =
          std::cos(spot->outerAngle * std::numbers::pi_v<float> / 180.0F);
      lighting.spotColorIntensity[base] = spot->color.r;
      lighting.spotColorIntensity[base + 1U] = spot->color.g;
      lighting.spotColorIntensity[base + 2U] = spot->color.b;
      lighting.spotColorIntensity[base + 3U] = spot->intensity;
      lighting.spotInner[base] =
          std::cos(spot->innerAngle * std::numbers::pi_v<float> / 180.0F);
      hasAuthoredLighting = true;
    }
  }
  if (hasAuthoredLighting && !hasEnvironment)
    lighting.ambient = {0.16F, 0.18F, 0.22F, 1.0F};
  if (hasAuthoredLighting && lighting.direction[3] == 0.0F &&
      pointCount == 0U && spotCount == 0U)
    lighting.directionalColor = {};
  return lighting;
}

} // namespace demi::runtime::render
