#include "demi/runtime/scripting/bindings/components/LuaRigidbody2DBindings.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include <sol/sol.hpp>
namespace demi::runtime {
void LuaRigidbody2DBindingModule::install(LuaScriptHost &host,
                                          lua_State *state) const {
  sol::table body = sol::state_view(state).create_named_table("Rigidbody2D");
  body.set_function("get_velocity", [state, &host](const std::string &id) {
    return luaVec2Result(state, host.getRigidbodyVelocity(id));
  });
  body.set_function("set_velocity",
                    [&host](const std::string &id, float x, float y) {
                      return host.setRigidbodyVelocity(id, x, y);
                    });
  body.set_function("set_velocity_x", [&host](const std::string &id, float x) {
    return host.setRigidbodyVelocityX(id, x);
  });
  body.set_function("set_velocity_y", [&host](const std::string &id, float y) {
    return host.setRigidbodyVelocityY(id, y);
  });
  body.set_function("add_impulse",
                    [&host](const std::string &id, float x, float y) {
                      return host.addRigidbodyImpulse(id, x, y);
                    });
}
} // namespace demi::runtime
