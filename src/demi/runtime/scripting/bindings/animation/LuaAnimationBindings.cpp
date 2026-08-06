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
  animation.set_function("set_speed", [&host](const std::string &entityId,
                                               float speed) {
    return host.setAnimationSpeed(entityId, speed);
  });
  animation.set_function("normalized_time",
                         [&host](const std::string &entityId) {
                           return host.animationNormalizedTime(entityId);
                         });
  animation.set_function("transition",
                         [&host, lua = sol::state_view(state)](
                             const std::string &entityId) mutable {
                           sol::table result = lua.create_table();
                           result["from"] =
                               host.animationTransitionFrom(entityId);
                           result["to"] = host.animationTransitionTo(entityId);
                           result["progress"] =
                               host.animationTransitionProgress(entityId);
                           result["active"] =
                               !host.animationTransitionTo(entityId).empty();
                           return result;
                         });
  animation.set_function(
      "set_layer_weight",
      [&host](const std::string &entityId, const std::string &layer,
              float weight) {
        return host.setAnimationLayerWeight(entityId, layer, weight);
      });
  animation.set_function("set_root_motion",
                         [&host](const std::string &entityId, bool enabled) {
                           return host.setAnimationRootMotion(entityId,
                                                              enabled);
                         });
}

} // namespace demi::runtime
