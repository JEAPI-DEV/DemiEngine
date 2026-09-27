#include "demi/runtime/scripting/LuaScriptHostInternal.h"
#include "demi/runtime/scripting/LuaServiceModules.h"
#include "demi/runtime/scripting/LuaGcTelemetry.h"

namespace demi::runtime {
void clearLuaBindingGlobals(lua_State *state) {
  clearLuaServiceModules(state);
  collectLuaGarbage(state);
}
} // namespace demi::runtime
