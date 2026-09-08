#include "demi/runtime/scripting/bindings/math/LuaVectorMathBindings.h"

#include "demi/runtime/math/VectorMath.h"

#include <sol/sol.hpp>

#include <vector>

namespace demi::runtime {
namespace {

Vec2 vec2(const sol::table value) {
  return {value.get_or(1, 0.0F), value.get_or(2, 0.0F)};
}

Vec3 vec3(const sol::table value) {
  return {value.get_or(1, 0.0F), value.get_or(2, 0.0F), value.get_or(3, 0.0F)};
}

sol::table table(lua_State *state, const Vec2 value) {
  sol::table result = sol::state_view(state).create_table();
  result[1] = value.x;
  result[2] = value.y;
  return result;
}

sol::table table(lua_State *state, const Vec3 value) {
  sol::table result = sol::state_view(state).create_table();
  result[1] = value.x;
  result[2] = value.y;
  result[3] = value.z;
  return result;
}

} // namespace

void LuaVectorMathBindingModule::install(LuaScriptHost &,
                                         lua_State *state) const {
  sol::state_view lua(state);
  sol::table vector2 = lua.create_named_table("Vector2");
  vector2.set_function(
      "add", [state](const sol::table left, const sol::table right) {
        return table(state, math::add(vec2(left), vec2(right)));
      });
  vector2.set_function(
      "subtract", [state](const sol::table left, const sol::table right) {
        return table(state, math::subtract(vec2(left), vec2(right)));
      });
  vector2.set_function("scale", [state](const sol::table value, float amount) {
    return table(state, math::scale(vec2(value), amount));
  });
  vector2.set_function("length", [](const sol::table value) {
    return math::length(vec2(value));
  });
  vector2.set_function("normalized", [state](const sol::table value) {
    return table(state, math::normalized(vec2(value)));
  });
  vector2.set_function("dot",
                       [](const sol::table left, const sol::table right) {
                         return math::dot(vec2(left), vec2(right));
                       });
  vector2.set_function("lerp", [state](const sol::table from,
                                       const sol::table to, float amount) {
    return table(state, math::lerp(vec2(from), vec2(to), amount));
  });

  sol::table vector3 = lua.create_named_table("Vector3");
  vector3.set_function(
      "add", [state](const sol::table left, const sol::table right) {
        return table(state, math::add(vec3(left), vec3(right)));
      });
  vector3.set_function(
      "subtract", [state](const sol::table left, const sol::table right) {
        return table(state, math::subtract(vec3(left), vec3(right)));
      });
  vector3.set_function("scale", [state](const sol::table value, float amount) {
    return table(state, math::scale(vec3(value), amount));
  });
  vector3.set_function("length", [](const sol::table value) {
    return math::length(vec3(value));
  });
  vector3.set_function("normalized", [state](const sol::table value) {
    return table(state, math::normalized(vec3(value)));
  });
  vector3.set_function("dot",
                       [](const sol::table left, const sol::table right) {
                         return math::dot(vec3(left), vec3(right));
                       });
  vector3.set_function(
      "cross", [state](const sol::table left, const sol::table right) {
        return table(state, math::cross(vec3(left), vec3(right)));
      });
  vector3.set_function("lerp", [state](const sol::table from,
                                       const sol::table to, float amount) {
    return table(state, math::lerp(vec3(from), vec3(to), amount));
  });

  sol::table scalar = lua.create_named_table("Mathf");
  scalar.set_function("smoothstep", &math::smoothstep);
}

} // namespace demi::runtime
