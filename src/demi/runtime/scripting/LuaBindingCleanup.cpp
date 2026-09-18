#include "demi/runtime/scripting/LuaScriptHostInternal.h"
#include "demi/runtime/scripting/LuaServiceModules.h"

namespace demi::runtime {
void clearLuaBindingGlobals(lua_State *state) {
  clearLuaServiceModules(state);
  lua_gc(state, LUA_GCCOLLECT, 0);
}
} // namespace demi::runtime
