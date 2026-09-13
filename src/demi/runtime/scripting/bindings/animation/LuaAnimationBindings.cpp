#include "demi/runtime/scripting/bindings/animation/LuaAnimationBindings.h"

#include "demi/runtime/animation/ProceduralIk.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

#include <sol/sol.hpp>

namespace demi::runtime {
namespace {

sol::table resultTable(lua_State *state, const TwoBoneIkResult2D &result) {
  sol::table table = sol::state_view(state).create_table();
  table["joint"] =
      sol::as_table(std::vector<float>{result.joint.x, result.joint.y});
  table["end_position"] =
      sol::as_table(std::vector<float>{result.end.x, result.end.y});
  table["reached"] = result.reached;
  return table;
}

sol::table resultTable(lua_State *state, const TwoBoneIkResult3D &result) {
  sol::table table = sol::state_view(state).create_table();
  table["joint"] = sol::as_table(
      std::vector<float>{result.joint.x, result.joint.y, result.joint.z});
  table["end_position"] = sol::as_table(
      std::vector<float>{result.end.x, result.end.y, result.end.z});
  table["reached"] = result.reached;
  return table;
}

} // namespace

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
  animation.set_function(
      "solve_two_bone_2d", [state](const sol::table options) -> sol::object {
        const auto result = solveTwoBoneIk2D(
            luaVec2Field(options, "root"), luaVec2Field(options, "target"),
            luaVec2Field(options, "pole"), options.get_or("upper_length", 0.0F),
            options.get_or("lower_length", 0.0F));
        return result ? sol::make_object(state, resultTable(state, *result))
                      : sol::make_object(state, sol::nil);
      });
  animation.set_function(
      "solve_two_bone_3d", [state](const sol::table options) -> sol::object {
        const auto result = solveTwoBoneIk3D(
            luaVec3Field(options, "root"), luaVec3Field(options, "target"),
            luaVec3Field(options, "pole"), options.get_or("upper_length", 0.0F),
            options.get_or("lower_length", 0.0F));
        return result ? sol::make_object(state, resultTable(state, *result))
                      : sol::make_object(state, sol::nil);
      });
  animation.set_function(
      "set_bone_segment",
      [&host](const std::string &entityId, const std::string &bone,
              const sol::table options) {
        return host.setAnimationBoneSegment(
            entityId, bone, luaVec3Field(options, "start"),
            luaVec3Field(options, "tail"), luaVec3Field(options, "pole"));
      });
  animation.set_function("clear_bone_segments",
                         [&host](const std::string &entityId) {
                           return host.clearAnimationBoneSegments(entityId);
                         });
}

} // namespace demi::runtime
