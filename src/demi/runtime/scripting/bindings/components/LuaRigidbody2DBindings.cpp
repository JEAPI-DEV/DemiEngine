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
  body.set_function("add_force",
                    [&host](const std::string &id, float x, float y) {
                      return host.addRigidbodyForce(id, x, y);
                    });
  body.set_function("add_torque", [&host](const std::string &id, float torque) {
    return host.addRigidbodyTorque(id, torque);
  });
  body.set_function("set_angular_velocity",
                    [&host](const std::string &id, float velocity) {
                      return host.setRigidbodyAngularVelocity(id, velocity);
                    });
  body.set_function("set_awake", [&host](const std::string &id, bool awake) {
    return host.setRigidbodyAwake(id, awake);
  });
  body.set_function("set_enabled",
                    [&host](const std::string &id, bool enabled) {
                      return host.setRigidbodyEnabled(id, enabled);
                    });
  body.set_function("move_kinematic", [&host](const std::string &id, float x,
                                              float y,
                                              sol::optional<float> fixedDt) {
    return host.moveKinematicBody(id, x, y, fixedDt.value_or(1.0F / 60.0F));
  });
  body.set_function(
      "move_and_slide",
      [state, &host](const std::string &id, const float x, const float y) {
        return luaVec2Result(state, host.moveAndSlideKinematic(id, x, y));
      });
}
} // namespace demi::runtime
