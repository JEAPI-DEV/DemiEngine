#include "demi/runtime/render/bgfx3d/SceneLighting3D.h"
#include "demi/runtime/render/bgfx3d/WorldTextProjection3D.h"
#include "demi/runtime/scene/components/3dcomponents/DirectionalLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Environment3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/PointLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SpotLightComponent.h"
#include <cmath>
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/WorldText3DComponent.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

using namespace demi::runtime;
using namespace demi::runtime::render;

int main() {
  World world;
  const SceneLighting3D defaults = collectSceneLighting3D(world, {});
  assert(defaults.ambient[0] == 1.0F);
  assert(defaults.shadowCascades == 4);
  assert(defaults.shadowSplitLambda == .85F);
  assert(defaults.shadowBlend == .1F);
  assert(defaults.shadowFilter == "pcf");
  assert(defaults.shadowResolution == 1024);
  assert(defaults.shadowDistance == 80.F);
  assert(defaults.shadowBias == .005F);
  assert(defaults.shadowBudget == 1);

  Entity environment;
  environment.id = "environment";
  environment.setComponent(
      Environment3DComponent{.ambientColor = {0.5F, 0.25F, 1.0F, 1.0F},
                             .ambientIntensity = 0.4F,
                             .shadowDistance = 50.F,
                             .shadowResolution = 512,
                             .shadowCascades = 2,
                             .shadowSplitLambda = .6F,
                             .shadowBlend = .2F,
                             .shadowFilter = "hard",
                             .shadowBias = .01F,
                             .maxShadowLights = 2});
  world.entities.push_back(std::move(environment));

  Entity maskedLight;
  maskedLight.id = "masked-light";
  maskedLight.setComponent(
      DirectionalLightComponent{.color = {1.0F, 0.0F, 0.0F, 1.0F},
                                .intensity = 3.0F,
                                .castsShadows = true,
                                .renderMask = "foreground"});
  world.entities.push_back(std::move(maskedLight));

  const SceneLighting3D background =
      collectSceneLighting3D(world, "background");
  assert(background.ambient[0] == 0.2F);
  assert(background.direction[3] == 0.0F);
  assert(!background.castsShadows);
  const SceneLighting3D foreground =
      collectSceneLighting3D(world, "foreground");
  assert(foreground.direction[3] == 3.0F);
  assert(foreground.castsShadows && foreground.shadowResolution == 512);
  assert(foreground.shadowDistance == 50.F);
  assert(foreground.shadowCascades == 2);
  assert(foreground.shadowSplitLambda == .6F);
  assert(foreground.shadowBlend == .2F);
  assert(foreground.shadowFilter == "hard");
  assert(foreground.shadowBias == .01F);
  assert(foreground.shadowBudget == 2);
  assert(foreground.directionalColor[0] == 1.0F);

  // Every active light is retained; there is no four-light truncation.
  for (int index = 0; index < 32; ++index) {
    Entity point;
    point.id = "point-" + std::to_string(index);
    point.setComponent(Transform3DComponent{
        .position = {static_cast<float>(index), 0.0F, 0.0F}});
    point.setComponent(
        PointLightComponent{.intensity = 2.0F, .range = 10.0F + index});
    world.entities.push_back(std::move(point));
  }
  const SceneLighting3D complete = collectSceneLighting3D(world, {});
  assert(complete.lights.size() == 32);
  assert(complete.lights.back().position.x == 31.0F);
  assert(complete.lights.back().range == 41.0F);
  assert(complete.hasAdditionalLights());
  world.entities[1].setComponent(Transform3DComponent{.rotation = {0, 1.570796327F, 0}});
  const auto rotated = collectSceneLighting3D(world, "foreground");
  assert(std::abs(rotated.direction[0] + .3F) < .0001F);
  assert(std::abs(rotated.direction[2] - .4F) < .0001F);
  for (int index = 0; index < 12; ++index) {
    Entity spot;
    spot.id = "spot-" + std::to_string(index);
    spot.setComponent(Transform3DComponent{});
    spot.setComponent(SpotLightComponent{});
    world.entities.push_back(std::move(spot));
  }
  Entity secondSun;
  secondSun.id = "second-sun";
  secondSun.setComponent(DirectionalLightComponent{.intensity = 2});
  world.entities.push_back(std::move(secondSun));
  const auto many = collectSceneLighting3D(world, {});
  assert(many.lights.size() == 45);
  assert(many.lights.back().kind == SceneLightKind3D::Directional);
  assert(many.lights.back().intensity == 2);
  assert(many.direction[3] == 3); // The first matching sun retains shadow ownership.


  World textWorld;
  Entity visible;
  visible.id = "visible";
  visible.setComponent(Transform3DComponent{.position = {0.0F, 0.0F, 5.0F}});
  visible.setComponent(
      WorldText3DComponent{.text = "visible", .fontSize = 1.0F});
  textWorld.entities.push_back(std::move(visible));
  Entity behind;
  behind.id = "behind";
  behind.setComponent(Transform3DComponent{.position = {0.0F, 0.0F, -1.0F}});
  behind.setComponent(WorldText3DComponent{.text = "behind", .fontSize = 1.0F});
  textWorld.entities.push_back(std::move(behind));
  Entity tooFar;
  tooFar.id = "too-far";
  tooFar.setComponent(Transform3DComponent{.position = {0.0F, 0.0F, 8.0F}});
  tooFar.setComponent(WorldText3DComponent{
      .text = "too far", .fontSize = 1.0F, .maxDistance = 2.0F});
  textWorld.entities.push_back(std::move(tooFar));

  BgfxCameraFrame3D frame{.camera = Camera3DComponent{},
                          .viewportWidth = 320,
                          .viewportHeight = 180};
  const ui::UiDocument perspective = projectWorldText3D(textWorld, frame);
  assert(perspective.nodes.size() == 1);
  assert(perspective.nodes.front().id == "world_text:visible");
  frame.camera.perspective = false;
  frame.camera.orthographicSize = 10.0F;
  const ui::UiDocument orthographic = projectWorldText3D(textWorld, frame);
  assert(orthographic.nodes.size() == 1);
  return 0;
}
