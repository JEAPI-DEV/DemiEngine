#include "demi/runtime/animation/SpriteAnimationSystem.h"

#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime {

void SpriteAnimationSystem::update(World &world, const float deltaTime) const {
  world.animationEvents.clear();
  for (Entity &entity : world.entities) {
    auto *animator = entity.component<SpriteAnimator2DComponent>();
    if (animator == nullptr || !animator->playing)
      continue;
    const auto found = animator->clips.find(animator->clip);
    if (found == animator->clips.end())
      continue;

    const SpriteAnimationClip2D &clip = found->second;
    const int previousTick =
        static_cast<int>(std::floor(animator->time * clip.framesPerSecond));
    animator->time += std::max(deltaTime, 0.0F) * animator->speed;
    const int currentTick =
        static_cast<int>(std::floor(animator->time * clip.framesPerSecond));
    int localFrame = currentTick;
    if (clip.loop) {
      localFrame %= clip.frameCount;
    } else if (localFrame >= clip.frameCount) {
      localFrame = clip.frameCount - 1;
      animator->playing = false;
    }
    animator->currentFrame = clip.startFrame + std::max(localFrame, 0);

    const int clipEndTick = clip.loop ? currentTick : clip.frameCount - 1;
    const int lastTick =
        std::min(clipEndTick, previousTick + clip.frameCount);
    for (int tick = previousTick + 1; tick <= lastTick; ++tick) {
      const int eventFrame = tick % clip.frameCount;
      for (const SpriteAnimationEvent2D &event : clip.events)
        if (event.frame == eventFrame)
          world.animationEvents.push_back(
              {.entityId = entity.id,
               .clip = animator->clip,
               .name = event.name,
               .frame = eventFrame});
    }
  }
}

} // namespace demi::runtime
