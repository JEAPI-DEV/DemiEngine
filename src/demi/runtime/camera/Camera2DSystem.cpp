#include "demi/runtime/camera/Camera2DSystem.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime {

void Camera2DSystem::update(World &world, const float deltaTime) const {
  for (Entity &entity : world.entities) {
    auto *camera = entity.component<Camera2DComponent>();
    auto *transform = entity.component<Transform2DComponent>();
    if (camera == nullptr || transform == nullptr || camera->target.empty())
      continue;

    const Entity *target = findEntity(world, camera->target);
    if (target == nullptr)
      continue;
    Vec2 desired = worldPosition2D(world, *target);
    desired.x += camera->followOffset.x;
    desired.y += camera->followOffset.y;
    if (camera->hasBounds) {
      desired.x = std::clamp(desired.x, camera->boundsMin.x,
                             camera->boundsMax.x);
      desired.y = std::clamp(desired.y, camera->boundsMin.y,
                             camera->boundsMax.y);
    }
    const float blend =
        camera->followSpeed <= 0.0F
            ? 1.0F
            : 1.0F - std::exp(-camera->followSpeed *
                              std::max(deltaTime, 0.0F));
    transform->position.x += (desired.x - transform->position.x) * blend;
    transform->position.y += (desired.y - transform->position.y) * blend;
  }
}

} // namespace demi::runtime
