#include "demi/runtime/scripting/bindings/components/LuaCamera3DBindings.h"

#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

#include <sol/sol.hpp>

namespace demi::runtime {

void LuaCamera3DBindingModule::install(LuaScriptHost &host,
                                       lua_State *state) const {
  sol::table camera = sol::state_view(state).create_named_table("Camera3D");
  camera.set_function(
      "screen_ray",
      [state, &host](const std::string &id, float x, float y, float width,
                     float height) -> sol::object {
        const auto ray = host.cameraRay3D(id, x, y, width, height);
        if (!ray)
          return sol::make_object(state, sol::nil);
        sol::table result = sol::state_view(state).create_table();
        result["origin"] =
            sol::as_table(std::vector<float>{ray->origin.x, ray->origin.y,
                                             ray->origin.z});
        result["direction"] =
            sol::as_table(std::vector<float>{ray->direction.x,
                                             ray->direction.y,
                                             ray->direction.z});
        return sol::make_object(state, result);
      });
  camera.set_function(
      "world_to_screen",
      [state, &host](const std::string &id, float x, float y, float z,
                     float width, float height) -> sol::object {
        const auto result =
            host.cameraWorldToScreen3D(id, x, y, z, width, height);
        return result
                   ? sol::make_object(
                         state, sol::as_table(std::vector<float>{
                                    result->x, result->y}))
                   : sol::make_object(state, sol::nil);
      });
  camera.set_function(
      "screen_to_world",
      [state, &host](const std::string &id, float x, float y, float width,
                     float height, float distance) {
        return luaVec3Result(
            state, host.cameraScreenToWorld3D(id, x, y, width, height,
                                              distance));
      });
}

} // namespace demi::runtime
