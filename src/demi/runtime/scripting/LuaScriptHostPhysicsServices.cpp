#include "demi/runtime/scripting/LuaScriptHost.h"

extern "C" {
#include <lua.h>
}

namespace demi::runtime {

void LuaScriptHost::dispatchPhysicsEvents() {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr || world_ == nullptr)
    return;
  const bool contact2D = hasEventListener("physics_contact");
  const bool collisionEnter2D = hasEventListener("physics_collision_enter");
  const bool collisionStay2D = hasEventListener("physics_collision_stay");
  const bool collisionExit2D = hasEventListener("physics_collision_exit");
  const bool triggerEnter2D = hasEventListener("physics_trigger_enter");
  const bool triggerStay2D = hasEventListener("physics_trigger_stay");
  const bool triggerExit2D = hasEventListener("physics_trigger_exit");
  for (const PhysicsContact2D &contact : world_->physicsContacts) {
    const bool phaseListener =
        contact.isTrigger ? (contact.phase == "enter"  ? triggerEnter2D
                             : contact.phase == "exit" ? triggerExit2D
                                                       : triggerStay2D)
                          : (contact.phase == "enter"  ? collisionEnter2D
                             : contact.phase == "exit" ? collisionExit2D
                                                       : collisionStay2D);
    if (!contact2D && !phaseListener)
      continue;
    lua_newtable(state);
    lua_pushstring(state, contact.entityId.c_str());
    lua_setfield(state, -2, "entity_id");
    lua_pushstring(state, contact.otherEntityId.c_str());
    lua_setfield(state, -2, "other_entity_id");
    lua_pushstring(state, contact.otherLayer.c_str());
    lua_setfield(state, -2, "other_layer");
    lua_pushstring(state, contact.phase.c_str());
    lua_setfield(state, -2, "phase");
    lua_pushnumber(state, contact.point.x);
    lua_setfield(state, -2, "point_x");
    lua_pushnumber(state, contact.point.y);
    lua_setfield(state, -2, "point_y");
    lua_pushnumber(state, contact.normal.x);
    lua_setfield(state, -2, "normal_x");
    lua_pushnumber(state, contact.normal.y);
    lua_setfield(state, -2, "normal_y");
    lua_pushnumber(state, contact.normalImpulse);
    lua_setfield(state, -2, "normal_impulse");
    lua_pushboolean(state, contact.isTrigger);
    lua_setfield(state, -2, "is_trigger");
    if (phaseListener) {
      const std::string kind = contact.isTrigger ? "trigger" : "collision";
      (void)emitEvent("physics_" + kind + "_" + contact.phase,
                      lua_gettop(state));
    }
    if (contact2D)
      (void)emitEvent("physics_contact", lua_gettop(state));
    lua_pop(state, 1);
  }
  const bool contact3D = hasEventListener("physics3d_contact");
  const bool collisionEnter3D = hasEventListener("physics3d_collision_enter");
  const bool collisionStay3D = hasEventListener("physics3d_collision_stay");
  const bool collisionExit3D = hasEventListener("physics3d_collision_exit");
  const bool triggerEnter3D = hasEventListener("physics3d_trigger_enter");
  const bool triggerStay3D = hasEventListener("physics3d_trigger_stay");
  const bool triggerExit3D = hasEventListener("physics3d_trigger_exit");
  for (const PhysicsContact3D &contact : world_->physicsContacts3D) {
    const bool phaseListener =
        contact.isTrigger ? (contact.phase == "enter"  ? triggerEnter3D
                             : contact.phase == "exit" ? triggerExit3D
                                                       : triggerStay3D)
                          : (contact.phase == "enter"  ? collisionEnter3D
                             : contact.phase == "exit" ? collisionExit3D
                                                       : collisionStay3D);
    if (!contact3D && !phaseListener)
      continue;
    lua_newtable(state);
    lua_pushstring(state, contact.entityId.c_str());
    lua_setfield(state, -2, "entity_id");
    lua_pushstring(state, contact.otherEntityId.c_str());
    lua_setfield(state, -2, "other_entity_id");
    lua_pushstring(state, contact.otherLayer.c_str());
    lua_setfield(state, -2, "other_layer");
    lua_pushstring(state, contact.phase.c_str());
    lua_setfield(state, -2, "phase");
    lua_pushnumber(state, contact.point.x);
    lua_setfield(state, -2, "point_x");
    lua_pushnumber(state, contact.point.y);
    lua_setfield(state, -2, "point_y");
    lua_pushnumber(state, contact.point.z);
    lua_setfield(state, -2, "point_z");
    lua_pushnumber(state, contact.normal.x);
    lua_setfield(state, -2, "normal_x");
    lua_pushnumber(state, contact.normal.y);
    lua_setfield(state, -2, "normal_y");
    lua_pushnumber(state, contact.normal.z);
    lua_setfield(state, -2, "normal_z");
    lua_pushnumber(state, contact.penetration);
    lua_setfield(state, -2, "penetration");
    lua_pushboolean(state, contact.isTrigger);
    lua_setfield(state, -2, "is_trigger");
    if (phaseListener) {
      const std::string kind = contact.isTrigger ? "trigger" : "collision";
      (void)emitEvent("physics3d_" + kind + "_" + contact.phase,
                      lua_gettop(state));
    }
    if (contact3D)
      (void)emitEvent("physics3d_contact", lua_gettop(state));
    lua_pop(state, 1);
  }
}

} // namespace demi::runtime
