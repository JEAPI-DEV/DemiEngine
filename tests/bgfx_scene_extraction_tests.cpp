#include "demi/runtime/render/bgfx3d/SceneLighting3D.h"
#include "demi/runtime/render/bgfx3d/WorldTextProjection3D.h"
#include "demi/runtime/scene/EntityComponentQuery.h"
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
  EntityComponentQuery lightingQuery;
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
      collectSceneLighting3D(world, "background", &lightingQuery);
  assert(background.ambient[0] == 0.2F);
  assert(background.direction[3] == 0.0F);
  assert(!background.castsShadows);
  const SceneLighting3D foreground =
      collectSceneLighting3D(world, "foreground", &lightingQuery);
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
  const SceneLighting3D complete = collectSceneLighting3D(world, {}, &lightingQuery);
  assert(complete.lights.size() == 32);
  assert(complete.lights.back().position.x == 31.0F);
  assert(complete.lights.back().range == 41.0F);
  assert(complete.hasAdditionalLights());
  world.entities[1].setComponent(Transform3DComponent{.rotation = {0, 1.570796327F, 0}});
  const auto rotated = collectSceneLighting3D(world, "foreground", &lightingQuery);
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
  const auto many = collectSceneLighting3D(world, {}, &lightingQuery);
  assert(many.lights.size() == 45);
  assert(many.lights.back().kind == SceneLightKind3D::Directional);
  assert(many.lights.back().intensity == 2);
  assert(many.direction[3] == 3); // The first matching sun retains shadow ownership.

  // Cached membership retains world order but always reads current values,
  // masks and enabled state. Component edits must invalidate membership too.
  world.entities[1].component<DirectionalLightComponent>()->intensity = 5;
  world.entities[0].component<Environment3DComponent>()->msaaSamples = 8;
  const auto edited = collectSceneLighting3D(world, {}, &lightingQuery);
  assert(edited.direction[3] == 5);
  assert(edited.msaaSamples == 8);
  world.entities[1].enabled = false;
  const auto disabled = collectSceneLighting3D(world, {}, &lightingQuery);
  assert(disabled.direction[3] == 2);
  assert(disabled.lights.size() == 44);
  world.entities[1].enabled = true;
  world.entities[1].removeComponent<DirectionalLightComponent>();
  const auto removed = collectSceneLighting3D(world, {}, &lightingQuery);
  assert(removed.direction[3] == 2);
  assert(removed.lights.size() == 44);
  world.entities[1].setComponent(
      DirectionalLightComponent{.intensity = 7, .renderMask = "foreground"});
  const auto reinserted = collectSceneLighting3D(world, {}, &lightingQuery);
  assert(reinserted.direction[3] == 7);
  assert(reinserted.lights.size() == 45);
  const auto maskedAgain =
      collectSceneLighting3D(world, "background", &lightingQuery);
  assert(maskedAgain.direction[3] == 2);
  assert(maskedAgain.lights.size() == 44);

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
  const ui::UiDocument perspective = projectWorldText3D(textWorld, frame, &lightingQuery);
  assert(perspective.nodes.size() == 1);
  assert(perspective.nodes.front().id == "world_text:visible");
  frame.camera.perspective = false;
  frame.camera.orthographicSize = 10.0F;
  const ui::UiDocument orthographic = projectWorldText3D(textWorld, frame, &lightingQuery);
  assert(orthographic.nodes.size() == 1);
  auto &liveText = textWorld.entities.front();
  liveText.component<WorldText3DComponent>()->text = "edited";
  assert(projectWorldText3D(textWorld, frame, &lightingQuery).nodes.front().text == "edited");
  liveText.component<WorldText3DComponent>()->renderMask = "foreground";
  frame.camera.renderMask = "background";
  assert(projectWorldText3D(textWorld, frame, &lightingQuery).nodes.empty());
  frame.camera.renderMask = "foreground";
  liveText.enabled = false;
  assert(projectWorldText3D(textWorld, frame, &lightingQuery).nodes.empty());
  liveText.enabled = true;
  liveText.removeComponent<WorldText3DComponent>();
  assert(projectWorldText3D(textWorld, frame, &lightingQuery).nodes.empty());
  liveText.setComponent(WorldText3DComponent{.text = "replacement"});
  assert(projectWorldText3D(textWorld, frame, &lightingQuery).nodes.front().text == "replacement");
  liveText.component<WorldText3DComponent>()->text.clear();
  assert(projectWorldText3D(textWorld, frame, &lightingQuery).nodes.empty());
  return 0;
}
