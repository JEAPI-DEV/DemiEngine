#include "demi/runtime/scripting/LuaScriptHost.h"

#if DEMI_HAS_LUA54
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#endif

namespace demi::runtime {

Diagnostics LuaScriptHost::checkScriptSyntax(const std::filesystem::path& path) {
  Diagnostics diagnostics;
  if (!std::filesystem::exists(path)) {
    diagnostics.push_back(Diagnostic{.severity = Severity::Error, .code = "SCRIPT_NOT_FOUND", .message = "Lua script does not exist.", .path = path.string(), .suggestion = "Pass an existing .lua script path."});
    return diagnostics;
  }

#if !DEMI_HAS_LUA54
  diagnostics.push_back(Diagnostic{.severity = Severity::Warning, .code = "SCRIPT_CHECK_LUA_UNAVAILABLE", .message = "Lua 5.4 was not found at configure time, so syntax could not be checked.", .path = path.string(), .suggestion = "Install lua5.4 development files and re-run cmake --preset linux-debug."});
#else
  auto* state = luaL_newstate();
  if (state == nullptr) {
    diagnostics.push_back(Diagnostic{.severity = Severity::Error, .code = "SCRIPT_CHECK_LUA_ALLOC_FAILED", .message = "Failed to allocate a Lua state for syntax checking.", .path = path.string(), .suggestion = "Retry after freeing memory."});
    return diagnostics;
  }

  if (luaL_loadfile(state, path.string().c_str()) != LUA_OK) {
    diagnostics.push_back(Diagnostic{.severity = Severity::Error, .code = "SCRIPT_SYNTAX_ERROR", .message = lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1) : "Lua syntax error.", .path = path.string(), .suggestion = "Fix the Lua parser error and run demi script check again."});
    lua_pop(state, 1);
  }
  lua_close(state);
#endif
  return diagnostics;
}

} // namespace demi::runtime
