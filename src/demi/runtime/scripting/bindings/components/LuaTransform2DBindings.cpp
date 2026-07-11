#include "demi/runtime/scripting/bindings/components/LuaTransform2DBindings.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include <sol/sol.hpp>
namespace demi::runtime {
void LuaTransform2DBindingModule::install(LuaScriptHost &host,
                                          lua_State *state) const {
  sol::table transform = sol::state_view(state).create_named_table("Transform");
  transform.set_function("get_position", [state, &host](const std::string &id) {
    return luaVec2Result(state, host.entityPosition(id));
  });
  transform.set_function("set_position",
                         [&host](const std::string &id, float x, float y) {
                           return host.setEntityPosition(id, x, y);
                         });
  transform.set_function("add_position",
                         [&host](const std::string &id, float x, float y) {
                           return host.addEntityPosition(id, x, y);
                         });
  transform.set_function("get_rotation", [&host](const std::string &id) {
    return host.entityRotation(id);
  });
  transform.set_function("set_rotation",
                         [&host](const std::string &id, float value) {
                           return host.setEntityRotation(id, value);
                         });
  transform.set_function("get_scale", [state, &host](const std::string &id) {
    return luaVec2Result(state, host.entityScale(id));
  });
  transform.set_function("set_scale",
                         [&host](const std::string &id, float x, float y) {
                           return host.setEntityScale(id, x, y);
                         });
}
} // namespace demi::runtime
