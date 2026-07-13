#include "demi/runtime/scripting/bindings/components/LuaSprite2DBindings.h"

#include <sol/sol.hpp>

namespace demi::runtime {

void LuaSprite2DBindingModule::install(LuaScriptHost &host,
                                       lua_State *state) const {
  sol::table sprite = sol::state_view(state).create_named_table("Sprite2D");
  sprite.set_function("play_animation", [&host](
                                            const std::string &entityId,
                                            const std::string &clip,
                                            const sol::optional<bool> restart) {
    return host.playSpriteAnimation(entityId, clip, restart.value_or(false));
  });
  sprite.set_function("pause_animation", [&host](const std::string &entityId) {
    return host.setSpriteAnimationPlaying(entityId, false);
  });
  sprite.set_function("resume_animation", [&host](const std::string &entityId) {
    return host.setSpriteAnimationPlaying(entityId, true);
  });
  sprite.set_function("current_animation",
                      [&host](const std::string &entityId) {
                        const auto clip = host.spriteAnimationClip(entityId);
                        return clip.value_or("");
                      });
  sprite.set_function("set_flip", [&host](const std::string &entityId,
                                          const bool flipX, const bool flipY) {
    return host.setSpriteFlip(entityId, flipX, flipY);
  });
}

} // namespace demi::runtime
