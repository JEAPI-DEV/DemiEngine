#include "demi/runtime/scripting/bindings/components/LuaTransform3DBindings.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include <sol/sol.hpp>
namespace demi::runtime {
void LuaTransform3DBindingModule::install(LuaScriptHost &host,
                                          lua_State *state) const {
  sol::table transform =
      sol::state_view(state).create_named_table("Transform3D");
  transform.set_function("get_position", [state, &host](const std::string &id) {
    return luaVec3Result(state, host.entityPosition3D(id));
  });
  transform.set_function("set_position", [&host](const std::string &id, float x,
                                                 float y, float z) {
    return host.setEntityPosition3D(id, x, y, z);
  });
  transform.set_function("add_position", [&host](const std::string &id, float x,
                                                 float y, float z) {
    return host.addEntityPosition3D(id, x, y, z);
  });
  transform.set_function("get_rotation", [state, &host](const std::string &id) {
    return luaVec3Result(state, host.entityRotation3D(id));
  });
  transform.set_function("set_rotation", [&host](const std::string &id, float x,
                                                 float y, float z) {
    return host.setEntityRotation3D(id, x, y, z);
  });
  transform.set_function("get_scale", [state, &host](const std::string &id) {
    return luaVec3Result(state, host.entityScale3D(id));
  });
  transform.set_function(
      "set_scale", [&host](const std::string &id, float x, float y, float z) {
        return host.setEntityScale3D(id, x, y, z);
      });
}
} // namespace demi::runtime
