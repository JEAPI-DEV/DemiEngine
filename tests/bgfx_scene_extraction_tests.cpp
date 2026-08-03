#include "demi/runtime/render/bgfx3d/SceneLighting3D.h"
#include "demi/runtime/render/bgfx3d/WorldTextProjection3D.h"
#include "demi/runtime/scene/components/3dcomponents/DirectionalLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Environment3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/PointLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/WorldText3DComponent.h"

#include <cassert>

using namespace demi::runtime;
using namespace demi::runtime::render;

int main() {
  World world;
  const SceneLighting3D defaults = collectSceneLighting3D(world, {});
  assert(defaults.ambient[0] == 1.0F);

  Entity environment;
  environment.id = "environment";
  environment.setComponent(Environment3DComponent{
      .ambientColor = {0.5F, 0.25F, 1.0F, 1.0F}, .ambientIntensity = 0.4F});
  world.entities.push_back(std::move(environment));

  Entity maskedLight;
  maskedLight.id = "masked-light";
  maskedLight.setComponent(
      DirectionalLightComponent{.color = {1.0F, 0.0F, 0.0F, 1.0F},
                                .intensity = 3.0F,
                                .renderMask = "foreground"});
  world.entities.push_back(std::move(maskedLight));

  const SceneLighting3D background =
      collectSceneLighting3D(world, "background");
  assert(background.ambient[0] == 0.2F);
  assert(background.direction[3] == 0.0F);
  const SceneLighting3D foreground =
      collectSceneLighting3D(world, "foreground");
  assert(foreground.direction[3] == 3.0F);
  assert(foreground.directionalColor[0] == 1.0F);

  // The shader has a documented four-light budget. Extra lights must be
  // ignored deterministically rather than writing beyond the packed arrays.
  for (int index = 0; index < 6; ++index) {
    Entity point;
    point.id = "point-" + std::to_string(index);
    point.setComponent(Transform3DComponent{
        .position = {static_cast<float>(index), 0.0F, 0.0F}});
    point.setComponent(
        PointLightComponent{.intensity = 2.0F, .range = 10.0F + index});
    world.entities.push_back(std::move(point));
  }
  const SceneLighting3D capped = collectSceneLighting3D(world, {});
  assert(capped.pointPositionRange[12] == 3.0F);
  assert(capped.pointPositionRange[15] == 13.0F);

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
