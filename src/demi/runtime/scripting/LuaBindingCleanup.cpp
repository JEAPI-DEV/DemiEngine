#include "demi/runtime/scripting/LuaScriptHostInternal.h"

namespace demi::runtime {

#if DEMI_HAS_LUA54

void clearLuaBindingGlobals(lua_State* state) {
  constexpr const char* globals[] = {
    "Debug", "Input", "Entity", "Transform", "Transform3D", "Time", "Timer", "Events", "Scene", "Runtime",
    "Rigidbody2D", "Physics2D", "Hud", "Save", "Audio", "AudioSource", "Video", "Cutscene", "Network", "NetworkSession",
  };
  for (const char* global : globals) {
    lua_pushnil(state);
    lua_setglobal(state, global);
  }
  lua_gc(state, LUA_GCCOLLECT, 0);
}

#endif

} // namespace demi::runtime
