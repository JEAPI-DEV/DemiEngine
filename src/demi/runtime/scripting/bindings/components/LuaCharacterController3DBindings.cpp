#include "demi/runtime/scripting/bindings/components/LuaCharacterController3DBindings.h"

#include "demi/runtime/scripting/LuaScriptHost.h"

#include <sol/sol.hpp>

namespace demi::runtime {

void LuaCharacterController3DBindingModule::install(LuaScriptHost &host,
                                                    lua_State *state) const {
  sol::table controller =
      sol::state_view(state).create_named_table("CharacterController3D");
  controller.set_function(
      "set_velocity",
      [&host](const std::string &id, float x, float y, float z) {
        return host.setCharacterVelocity3D(id, x, y, z);
      });
  controller.set_function("jump",
                          [&host](const std::string &id, float speed) {
                            return host.requestCharacterJump3D(id, speed);
                          });
  controller.set_function(
      "state", [state, &host](const std::string &id) -> sol::object {
        const auto value = host.characterState3D(id);
        if (!value)
          return sol::make_object(state, sol::nil);
        sol::table result = sol::state_view(state).create_table();
        result["velocity"] = sol::as_table(
            std::vector<float>{value->appliedMotion.x, value->appliedMotion.y,
                               value->appliedMotion.z});
        result["grounded"] = value->grounded;
        result["ground_entity"] = value->groundEntity;
        return sol::make_object(state, result);
      });
}

} // namespace demi::runtime
