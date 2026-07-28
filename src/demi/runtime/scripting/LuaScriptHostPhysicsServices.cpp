#include "demi/runtime/scripting/LuaScriptHost.h"

extern "C" {
#include <lua.h>
}

namespace demi::runtime {

void LuaScriptHost::dispatchPhysicsEvents() {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr || world_ == nullptr)
    return;
  for (const PhysicsContact2D &contact : world_->physicsContacts) {
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
    const std::string kind = contact.isTrigger ? "trigger" : "collision";
    (void)emitEvent("physics_" + kind + "_" + contact.phase, lua_gettop(state));
    (void)emitEvent("physics_contact", lua_gettop(state));
    lua_pop(state, 1);
  }
}

} // namespace demi::runtime
