#include "demi/runtime/scripting/LuaScriptHostInternal.h"

namespace demi::runtime {


void clearLuaBindingGlobals(lua_State* state) {
  constexpr const char* globals[] = {
    "Debug", "Input", "Entity", "Transform", "Transform3D", "Time", "Timer", "Events", "Scene", "Runtime",
    "Rigidbody2D", "Physics2D", "Hud", "Save", "Audio", "AudioSource", "Video", "Cutscene", "Network", "NetworkSession",
    "TlsServer", "TlsClient", "Crypto",
  };
  for (const char* global : globals) {
    lua_pushnil(state);
    lua_setglobal(state, global);
  }
  lua_gc(state, LUA_GCCOLLECT, 0);
}


} // namespace demi::runtime
