#include "demi/runtime/render/Lighting3D.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/DirectionalLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Environment3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/PointLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SpotLightComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime {
namespace {

bool maskMatches(const std::string &cameraMask, const std::string &lightMask) {
  return cameraMask.empty() || lightMask.empty() || cameraMask == lightMask;
}

void pushLight(LightingFrame3D &frame, const RenderLight3D &light) {
  if (frame.lightCount >= static_cast<int>(frame.lights.size()))
    return;
  frame.lights[static_cast<std::size_t>(frame.lightCount++)] = light;
}

float angleCos(const float degrees) {
  return std::cos(degrees * 0.017453292519943295F);
}

} // namespace

LightingFrame3D collectLighting3D(const World &world,
                                  const std::string &renderMask,
                                  RenderStatistics &statistics) {
  LightingFrame3D frame;
  int maxShadowLights = 1;
  for (const Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    if (const auto *environment = entity.component<Environment3DComponent>()) {
      frame.ambientColor = environment->ambientColor;
      frame.ambientIntensity = environment->ambientIntensity;
      frame.fogColor = environment->fogColor;
      frame.fogStart = environment->fogStart;
      frame.fogEnd = environment->fogEnd;
      maxShadowLights = environment->maxShadowLights;
      break;
    }
  }
  for (const Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    if (const auto *directional =
            entity.component<DirectionalLightComponent>()) {
      if (maskMatches(renderMask, directional->renderMask))
        pushLight(frame, {.direction = directional->direction,
                          .color = directional->color,
                          .intensity = directional->intensity,
                          .range = 1.0F,
                          .type = 0,
                          .castsShadows = directional->castsShadows});
    } else if (const auto *point = entity.component<PointLightComponent>();
               point != nullptr && maskMatches(renderMask, point->renderMask)) {
      pushLight(frame, {.position = worldPosition3D(world, entity),
                        .color = point->color,
                        .intensity = point->intensity,
                        .range = point->range,
                        .type = 1,
                        .castsShadows = point->castsShadows});
    } else if (const auto *spot = entity.component<SpotLightComponent>();
               spot != nullptr && maskMatches(renderMask, spot->renderMask)) {
      const auto transform = resolveWorldTransform3D(world, entity);
      const Vec3 direction =
          transform ? transformDirection3D(*transform, spot->direction)
                    : spot->direction;
      pushLight(frame, {.position = worldPosition3D(world, entity),
                        .direction = direction,
                        .color = spot->color,
                        .intensity = spot->intensity,
                        .range = spot->range,
                        .innerCos = angleCos(spot->innerAngle),
                        .outerCos = angleCos(spot->outerAngle),
                        .type = 2,
                        .castsShadows = spot->castsShadows});
    }
  }
  statistics.lights += static_cast<std::size_t>(frame.lightCount);
  for (int index = 0; index < frame.lightCount; ++index)
    if (frame.lights[static_cast<std::size_t>(index)].castsShadows &&
        frame.shadowLightCount < maxShadowLights)
      ++frame.shadowLightCount;
  statistics.shadowPasses +=
      static_cast<std::size_t>(frame.shadowLightCount);
  return frame;
}

void applyLighting3D(const LightingFrame3D &lighting, const Shader shader,
                     const Vec3 cameraPosition) {
  const float ambient[]{lighting.ambientColor.r, lighting.ambientColor.g,
                        lighting.ambientColor.b,
                        lighting.ambientIntensity};
  const float fog[]{lighting.fogColor.r, lighting.fogColor.g,
                    lighting.fogColor.b, lighting.fogColor.a};
  const float view[]{cameraPosition.x, cameraPosition.y, cameraPosition.z};
  SetShaderValue(shader, GetShaderLocation(shader, "ambient"), ambient,
                 SHADER_UNIFORM_VEC4);
  SetShaderValue(shader, GetShaderLocation(shader, "viewPos"), view,
                 SHADER_UNIFORM_VEC3);
  SetShaderValue(shader, GetShaderLocation(shader, "fogColor"), fog,
                 SHADER_UNIFORM_VEC4);
  SetShaderValue(shader, GetShaderLocation(shader, "fogStart"),
                 &lighting.fogStart, SHADER_UNIFORM_FLOAT);
  SetShaderValue(shader, GetShaderLocation(shader, "fogEnd"), &lighting.fogEnd,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(shader, GetShaderLocation(shader, "lightCount"),
                 &lighting.lightCount, SHADER_UNIFORM_INT);
  for (int index = 0; index < lighting.lightCount; ++index) {
    const auto &light = lighting.lights[static_cast<std::size_t>(index)];
    const std::string suffix = "[" + std::to_string(index) + "]";
    const float position[]{light.position.x, light.position.y,
                           light.position.z};
    const float direction[]{light.direction.x, light.direction.y,
                            light.direction.z};
    const float color[]{light.color.r, light.color.g, light.color.b,
                        light.intensity};
    SetShaderValue(shader,
                   GetShaderLocation(shader,
                                     ("lights" + suffix + ".position").c_str()),
                   position, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader,
                   GetShaderLocation(shader,
                                     ("lights" + suffix + ".direction").c_str()),
                   direction, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader,
                   GetShaderLocation(shader,
                                     ("lights" + suffix + ".color").c_str()),
                   color, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader,
                   GetShaderLocation(shader,
                                     ("lights" + suffix + ".range").c_str()),
                   &light.range, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader,
                   GetShaderLocation(shader,
                                     ("lights" + suffix + ".innerCos").c_str()),
                   &light.innerCos, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader,
                   GetShaderLocation(shader,
                                     ("lights" + suffix + ".outerCos").c_str()),
                   &light.outerCos, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader,
                   GetShaderLocation(shader,
                                     ("lights" + suffix + ".type").c_str()),
                   &light.type, SHADER_UNIFORM_INT);
  }
}

} // namespace demi::runtime
