#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/animation/AnimationStateMachineComponent.h"

extern "C" {
#include <lua.h>
}

namespace demi::runtime {

std::optional<std::string>
LuaScriptHost::animationState(const std::string &entityId) const {
  const Entity *entity =
      world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  const auto *machine =
      entity == nullptr ? nullptr
                        : entity->component<AnimationStateMachineComponent>();
  return machine == nullptr ? std::nullopt : std::make_optional(machine->state);
}

bool LuaScriptHost::playAnimationState(const std::string &entityId,
                                       const std::string &state) {
  Entity *entity = world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  auto *machine = entity == nullptr
                      ? nullptr
                      : entity->component<AnimationStateMachineComponent>();
  if (machine == nullptr || !machine->states.contains(state))
    return false;
  machine->state = state;
  machine->time = 0.0F;
  machine->entered = true;
  return true;
}

bool LuaScriptHost::setAnimationParameter(const std::string &entityId,
                                          const std::string &parameter,
                                          const float value) {
  Entity *entity = world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  auto *machine = entity == nullptr
                      ? nullptr
                      : entity->component<AnimationStateMachineComponent>();
  if (machine == nullptr)
    return false;
  machine->parameters[parameter] = value;
  return true;
}

bool LuaScriptHost::triggerAnimation(const std::string &entityId,
                                     const std::string &trigger) {
  Entity *entity = world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  auto *machine = entity == nullptr
                      ? nullptr
                      : entity->component<AnimationStateMachineComponent>();
  if (machine == nullptr)
    return false;
  machine->triggers.insert(trigger);
  return true;
}

bool LuaScriptHost::playSpriteAnimation(const std::string &entityId,
                                        const std::string &clip,
                                        const bool restart) {
  Entity *entity = world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  auto *animator = entity == nullptr
                       ? nullptr
                       : entity->component<SpriteAnimator2DComponent>();
  if (animator == nullptr || !animator->clips.contains(clip))
    return false;
  if (restart || animator->clip != clip) {
    animator->clip = clip;
    animator->time = 0.0F;
    animator->currentFrame = animator->clips.at(clip).startFrame;
  }
  animator->playing = true;
  return true;
}

bool LuaScriptHost::setSpriteAnimationPlaying(const std::string &entityId,
                                              const bool playing) {
  Entity *entity = world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  auto *animator = entity == nullptr
                       ? nullptr
                       : entity->component<SpriteAnimator2DComponent>();
  if (animator == nullptr)
    return false;
  animator->playing = playing;
  return true;
}

std::optional<std::string>
LuaScriptHost::spriteAnimationClip(const std::string &entityId) const {
  const Entity *entity =
      world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  const auto *animator = entity == nullptr
                             ? nullptr
                             : entity->component<SpriteAnimator2DComponent>();
  return animator == nullptr ? std::nullopt
                             : std::make_optional(animator->clip);
}

bool LuaScriptHost::setSpriteFlip(const std::string &entityId, const bool flipX,
                                  const bool flipY) {
  Entity *entity = world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  auto *sprite =
      entity == nullptr ? nullptr : entity->component<SpriteComponent>();
  if (sprite == nullptr)
    return false;
  sprite->flipX = flipX;
  sprite->flipY = flipY;
  return true;
}

bool LuaScriptHost::setSpriteSize(const std::string &entityId,
                                  const float width, const float height) {
  Entity *entity = world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  auto *sprite =
      entity == nullptr ? nullptr : entity->component<SpriteComponent>();
  if (sprite == nullptr || width < 0.0F || height < 0.0F)
    return false;
  sprite->size = {.x = width, .y = height};
  return true;
}

void LuaScriptHost::dispatchAnimationEvents() {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr || world_ == nullptr)
    return;
  for (const AnimationEvent2D &event : world_->animationEvents) {
    lua_newtable(state);
    lua_pushstring(state, event.entityId.c_str());
    lua_setfield(state, -2, "entity_id");
    lua_pushstring(state, event.clip.c_str());
    lua_setfield(state, -2, "clip");
    lua_pushstring(state, event.name.c_str());
    lua_setfield(state, -2, "name");
    lua_pushinteger(state, event.frame);
    lua_setfield(state, -2, "frame");
    (void)emitEvent("sprite_animation", lua_gettop(state));
    lua_pop(state, 1);
  }
  world_->animationEvents.clear();
  for (const AnimationEvent2D &event : world_->stateAnimationEvents) {
    lua_newtable(state);
    lua_pushstring(state, event.entityId.c_str());
    lua_setfield(state, -2, "entity_id");
    lua_pushstring(state, event.clip.c_str());
    lua_setfield(state, -2, "state");
    lua_pushstring(state, event.name.c_str());
    lua_setfield(state, -2, "name");
    (void)emitEvent("animation_event", lua_gettop(state));
    lua_pop(state, 1);
  }
  world_->stateAnimationEvents.clear();
}

void LuaScriptHost::dispatchAnimationCollisionEvents() {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr || world_ == nullptr)
    return;
  for (const AnimationCollisionOverlap2D &overlap :
       world_->animationCollisionOverlaps) {
    lua_newtable(state);
    lua_pushstring(state, overlap.sourceId.c_str());
    lua_setfield(state, -2, "source_id");
    lua_pushstring(state, overlap.targetId.c_str());
    lua_setfield(state, -2, "target_id");
    lua_pushstring(state, overlap.window.c_str());
    lua_setfield(state, -2, "window");
    lua_pushstring(state, overlap.receiver.c_str());
    lua_setfield(state, -2, "receiver");
    (void)emitEvent("animation_collision", lua_gettop(state));
    lua_pop(state, 1);
  }
  world_->animationCollisionOverlaps.clear();
}

} // namespace demi::runtime
