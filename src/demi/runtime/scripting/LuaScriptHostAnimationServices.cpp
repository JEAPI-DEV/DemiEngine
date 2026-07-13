#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"

extern "C" {
#include <lua.h>
}

namespace demi::runtime {

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
}

} // namespace demi::runtime
