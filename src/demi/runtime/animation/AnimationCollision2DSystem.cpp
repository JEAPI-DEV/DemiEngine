#include "demi/runtime/animation/AnimationCollision2DSystem.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/animation/AnimationCollision2DComponent.h"
#include "demi/runtime/scene/components/animation/AnimationStateMachineComponent.h"

#include <cmath>

namespace demi::runtime {
namespace {

struct Box {
  Vec2 center;
  Vec2 size;
};

bool overlaps(const Box &left, const Box &right) {
  return std::abs(left.center.x - right.center.x) * 2.0F <
             left.size.x + right.size.x &&
         std::abs(left.center.y - right.center.y) * 2.0F <
             left.size.y + right.size.y;
}

} // namespace

void AnimationCollision2DSystem::update(World &world) const {
  world.animationCollisionOverlaps.clear();
  for (Entity &source : world.entities) {
    auto *collision = source.component<AnimationCollision2DComponent>();
    const auto *machine = source.component<AnimationStateMachineComponent>();
    if (collision == nullptr || machine == nullptr)
      continue;
    const auto window = collision->windows.find(machine->state);
    const bool active = window != collision->windows.end() &&
                        machine->time >= window->second.start &&
                        machine->time <= window->second.end;
    if (!active) {
      collision->activeWindow.clear();
      collision->reportedOverlaps.clear();
      continue;
    }
    if (collision->activeWindow != machine->state) {
      collision->activeWindow = machine->state;
      collision->reportedOverlaps.clear();
    }
    const bool flipped = source.hasComponent<SpriteComponent>() &&
                         source.component<SpriteComponent>()->flipX;
    const float facing = flipped ? -1.0F : 1.0F;
    const Vec2 sourcePosition = worldPosition2D(world, source);
    const Box sourceBox{
        .center = {sourcePosition.x + window->second.offset.x * facing,
                   sourcePosition.y + window->second.offset.y},
        .size = window->second.size};
    for (const Entity &target : world.entities) {
      const auto *targetCollision =
          target.component<AnimationCollision2DComponent>();
      if (&target == &source || targetCollision == nullptr)
        continue;
      const Vec2 targetPosition = worldPosition2D(world, target);
      for (const auto &[receiverName, receiver] : targetCollision->receivers) {
        if (!window->second.mask.empty() &&
            window->second.mask != receiver.layer)
          continue;
        const std::string key = target.id + ":" + receiverName;
        if (collision->reportedOverlaps.contains(key))
          continue;
        const Box targetBox{.center = {targetPosition.x + receiver.offset.x,
                                       targetPosition.y + receiver.offset.y},
                            .size = receiver.size};
        if (!overlaps(sourceBox, targetBox))
          continue;
        collision->reportedOverlaps.insert(key);
        world.animationCollisionOverlaps.push_back({.sourceId = source.id,
                                                    .targetId = target.id,
                                                    .window = machine->state,
                                                    .receiver = receiverName});
      }
    }
  }
}

} // namespace demi::runtime
