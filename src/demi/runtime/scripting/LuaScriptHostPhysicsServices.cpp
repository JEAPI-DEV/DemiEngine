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
    if (!contact.isTrigger)
      continue;
    lua_newtable(state);
    lua_pushstring(state, contact.entityId.c_str());
    lua_setfield(state, -2, "entity_id");
    lua_pushstring(state, contact.otherEntityId.c_str());
    lua_setfield(state, -2, "other_entity_id");
    lua_pushstring(state, contact.otherLayer.c_str());
    lua_setfield(state, -2, "other_layer");
    lua_pushnumber(state, contact.normal.x);
    lua_setfield(state, -2, "normal_x");
    lua_pushnumber(state, contact.normal.y);
    lua_setfield(state, -2, "normal_y");
    (void)emitEvent("physics_trigger", lua_gettop(state));
    lua_pop(state, 1);
  }
}

} // namespace demi::runtime
