#include "demi/runtime/camera/Camera2DSystem.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <cmath>
#include <iostream>

using namespace demi::runtime;

int main() {
  Entity target;
  target.id = "player";
  target.setComponent(Transform2DComponent{.position = {20.0F, -4.0F}});

  Entity cameraEntity;
  cameraEntity.id = "camera";
  cameraEntity.setComponent(Transform2DComponent{});
  cameraEntity.setComponent(Camera2DComponent{.target = "player",
                                              .followSpeed = 0.0F,
                                              .boundsMin = {-5.0F, -2.0F},
                                              .boundsMax = {10.0F, 8.0F},
                                              .hasBounds = true});
  World world;
  world.entities.push_back(std::move(target));
  world.entities.push_back(std::move(cameraEntity));

  Camera2DSystem{}.update(world, 1.0F / 60.0F);
  const Vec2 snapped =
      world.entities.back().component<Transform2DComponent>()->position;
  if (snapped.x != 10.0F || snapped.y != -2.0F) {
    std::cerr << "Camera target bounds were not applied.\n";
    return 1;
  }

  auto *camera = world.entities.back().component<Camera2DComponent>();
  camera->hasBounds = false;
  camera->followSpeed = 2.0F;
  world.entities.back().component<Transform2DComponent>()->position = {};
  Camera2DSystem{}.update(world, 0.5F);
  const Vec2 smoothed =
      world.entities.back().component<Transform2DComponent>()->position;
  const float expectedX = 20.0F * (1.0F - std::exp(-1.0F));
  if (std::abs(smoothed.x - expectedX) > 0.0001F || smoothed.y >= 0.0F) {
    std::cerr << "Camera follow smoothing failed.\n";
    return 1;
  }
  Entity disabledCamera = world.entities[1];
  disabledCamera.id = "disabled_camera";
  disabledCamera.enabled = false;
  disabledCamera.component<Transform2DComponent>()->position = {7.0F, 9.0F};
  world.entities.push_back(std::move(disabledCamera));
  Entity cameraWithoutTransform;
  cameraWithoutTransform.id = "camera_without_transform";
  cameraWithoutTransform.setComponent(Camera2DComponent{.target = "player"});
  world.entities.push_back(std::move(cameraWithoutTransform));
  world.entities[0].enabled = false;
  Camera2DSystem{}.update(world, 0.5F);
  const Vec2 unchanged =
      world.entities[1].component<Transform2DComponent>()->position;
  const Vec2 disabledPosition =
      world.entities[2].component<Transform2DComponent>()->position;
  if (unchanged.x != smoothed.x || unchanged.y != smoothed.y ||
      disabledPosition.x != 7.0F || disabledPosition.y != 9.0F) {
    std::cerr << "Camera filtering advanced a disabled camera or target.\n";
    return 1;
  }
  return 0;
}
