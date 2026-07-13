#include "demi/runtime/animation/SpriteAnimationSystem.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"

#include <iostream>
#include <nlohmann/json.hpp>

using namespace demi::runtime;

int main() {
  Entity entity;
  entity.id = "player";
  SpriteAnimator2DComponent::parse(nlohmann::json::parse(R"({
    "frame_size": [16, 16],
    "clips": {
      "run": {"start_frame": 2, "frame_count": 3, "fps": 10,
              "events": [{"frame": 1, "name": "step"}]},
      "idle": {"start_frame": 0, "frame_count": 1, "fps": 2}
    }
  })"),
                                   entity);
  SpriteComponent::parse(nlohmann::json::parse(R"({
    "sorting_order": 4, "source_position": [2, 3],
    "source_size": [8, 9], "size": [2.5, 3.5], "pivot": [0.25, 0.75],
    "flip_x": true
  })"),
                         entity);

  auto *animator = entity.component<SpriteAnimator2DComponent>();
  const auto *sprite = entity.component<SpriteComponent>();
  if (animator == nullptr || animator->clip != "idle" ||
      animator->currentFrame != 0 || sprite == nullptr ||
      sprite->sortingOrder != 4 || sprite->sourcePosition.x != 2.0F ||
      sprite->sourceSize.y != 9.0F || sprite->size.x != 2.5F ||
      sprite->size.y != 3.5F || sprite->pivot.x != 0.25F ||
      !sprite->flipX) {
    std::cerr << "Sprite animation or presentation parsing failed.\n";
    return 1;
  }

  animator->clip = "run";
  animator->currentFrame = 2;
  World world;
  world.entities.push_back(std::move(entity));
  SpriteAnimationSystem system;
  system.update(world, 0.11F);
  animator = world.entities.front().component<SpriteAnimator2DComponent>();
  if (animator->currentFrame != 3 || world.animationEvents.size() != 1 ||
      world.animationEvents.front().name != "step" ||
      world.animationEvents.front().entityId != "player") {
    std::cerr << "Sprite frame advancement or event emission failed.\n";
    return 1;
  }

  animator->clips.at("run").loop = false;
  animator->time = 0.0F;
  animator->playing = true;
  system.update(world, 1.0F);
  if (animator->currentFrame != 4 || animator->playing ||
      world.animationEvents.size() != 1 ||
      world.animationEvents.front().name != "step") {
    std::cerr << "Non-looping sprite animation did not stop at its end.\n";
    return 1;
  }
  return 0;
}
