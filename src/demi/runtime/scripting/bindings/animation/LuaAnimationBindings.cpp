#include "demi/runtime/scripting/bindings/animation/LuaAnimationBindings.h"

#include <sol/sol.hpp>

namespace demi::runtime {

void LuaAnimationBindingModule::install(LuaScriptHost &host,
                                        lua_State *state) const {
  sol::table animation = sol::state_view(state).create_named_table("Animation");
  animation.set_function("state", [&host](const std::string &entityId) {
    return host.animationState(entityId).value_or("");
  });
  animation.set_function("play", [&host](const std::string &entityId,
                                         const std::string &stateName) {
    return host.playAnimationState(entityId, stateName);
  });
  animation.set_function("set_number", [&host](const std::string &entityId,
                                               const std::string &parameter,
                                               const float value) {
    return host.setAnimationParameter(entityId, parameter, value);
  });
  animation.set_function("set_bool", [&host](const std::string &entityId,
                                             const std::string &parameter,
                                             const bool value) {
    return host.setAnimationParameter(entityId, parameter, value ? 1.0F : 0.0F);
  });
  animation.set_function("trigger", [&host](const std::string &entityId,
                                            const std::string &trigger) {
    return host.triggerAnimation(entityId, trigger);
  });
}

} // namespace demi::runtime
