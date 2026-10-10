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
  bool hasDirectional = false;
  for (const Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    if (const auto *environment = entity.component<Environment3DComponent>()) {
      lighting.msaaSamples = environment->msaaSamples;
      lighting.skyTexture = environment->skyTexture;
      lighting.reliefCacheMeshes = environment->reliefCacheMeshes;
      lighting.reliefImageCacheBytes = environment->reliefImageCacheBytes;
      lighting.shadowDistance = environment->shadowDistance;
      lighting.shadowResolution = environment->shadowResolution;
      lighting.shadowCascades = environment->shadowCascades;
      lighting.shadowSplitLambda = environment->shadowSplitLambda;
      lighting.shadowBlend = environment->shadowBlend;
      lighting.shadowFilter = environment->shadowFilter;
      lighting.shadowBias = environment->shadowBias;
      lighting.shadowBudget = environment->maxShadowLights;
      lighting.ambient = {
          environment->ambientColor.r * environment->ambientIntensity,
          environment->ambientColor.g * environment->ambientIntensity,
          environment->ambientColor.b * environment->ambientIntensity, 1.0F};
      hasAuthoredLighting = true;
      hasEnvironment = true;
    }
    const auto transform = resolveWorldTransform3D(world, entity);
    if (const auto *directional = entity.component<DirectionalLightComponent>();
        directional != nullptr &&
        matchesMask(renderMask, directional->renderMask)) {
      const Vec3 direction =
          transform ? transformDirection3D(*transform, directional->direction)
                    : directional->direction;
      if (!hasDirectional) {
        lighting.direction = {direction.x, direction.y, direction.z,
                              directional->intensity};
        lighting.castsShadows =
            directional->castsShadows && directional->intensity > 0;
        lighting.directionalColor = {directional->color.r, directional->color.g,
                                     directional->color.b, 1.0F};
        hasDirectional = true;
      } else if (directional->intensity > 0) {
        lighting.lights.push_back({.kind = SceneLightKind3D::Directional,
                                   .color = directional->color,
                                   .intensity = directional->intensity,
                                   .direction = direction});
      }
      hasAuthoredLighting = true;
    }
    if (const auto *point = entity.component<PointLightComponent>();
        point && transform && point->range > 0 && point->intensity > 0 &&
        matchesMask(renderMask, point->renderMask)) {
      lighting.lights.push_back({.kind = SceneLightKind3D::Point,
                                 .position = transform->position,
                                 .range = point->range,
                                 .color = point->color,
                                 .intensity = point->intensity});
      hasAuthoredLighting = true;
    }
    if (const auto *spot = entity.component<SpotLightComponent>();
        spot && transform && spot->range > 0 && spot->intensity > 0 &&
        matchesMask(renderMask, spot->renderMask)) {
      const Vec3 direction = transformDirection3D(*transform, spot->direction);
      lighting.lights.push_back(
          {.kind = SceneLightKind3D::Spot,
           .position = transform->position,
           .range = spot->range,
           .color = spot->color,
           .intensity = spot->intensity,
           .direction = direction,
           .outerConeCos =
               std::cos(spot->outerAngle * std::numbers::pi_v<float> / 180.0F),
           .innerConeCos = std::cos(spot->innerAngle *
                                    std::numbers::pi_v<float> / 180.0F)});
      hasAuthoredLighting = true;
    }
  }
  if (hasAuthoredLighting && !hasEnvironment)
    lighting.ambient = {0.16F, 0.18F, 0.22F, 1.0F};
  if (hasAuthoredLighting && lighting.direction[3] == 0.0F &&
      lighting.lights.empty())
    lighting.directionalColor = {};
  return lighting;
}

} // namespace demi::runtime::render
