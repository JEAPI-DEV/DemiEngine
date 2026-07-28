#include "demi/runtime/scripting/bindings/components/LuaRigidbody3DBindings.h"

#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

#include <sol/sol.hpp>

namespace demi::runtime {

void LuaRigidbody3DBindingModule::install(LuaScriptHost &host,
                                          lua_State *state) const {
  sol::table body =
      sol::state_view(state).create_named_table("Rigidbody3D");
  body.set_function("get_velocity", [state, &host](const std::string &id) {
    return luaVec3Result(state, host.getRigidbodyVelocity3D(id));
  });
  body.set_function(
      "set_velocity",
      [&host](const std::string &id, float x, float y, float z) {
        return host.setRigidbodyVelocity3D(id, x, y, z);
      });
  body.set_function(
      "add_force", [&host](const std::string &id, float x, float y, float z) {
        return host.addRigidbodyForce3D(id, x, y, z);
      });
  body.set_function(
      "add_impulse",
      [&host](const std::string &id, float x, float y, float z) {
        return host.addRigidbodyImpulse3D(id, x, y, z);
      });
  body.set_function(
      "add_torque",
      [&host](const std::string &id, float x, float y, float z) {
        return host.addRigidbodyTorque3D(id, x, y, z);
      });
  body.set_function("set_awake",
                    [&host](const std::string &id, bool awake) {
                      return host.setRigidbodyAwake3D(id, awake);
                    });
  body.set_function("set_enabled",
                    [&host](const std::string &id, bool enabled) {
                      return host.setRigidbodyEnabled3D(id, enabled);
                    });
  body.set_function(
      "move_kinematic",
      [&host](const std::string &id, float x, float y, float z, float rotationX,
              float rotationY, float rotationZ, float fixedDt) {
        return host.moveKinematicBody3D(id, x, y, z, rotationX, rotationY,
                                       rotationZ, fixedDt);
      });
}

} // namespace demi::runtime
