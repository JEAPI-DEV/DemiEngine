#pragma once

#include "demi/runtime/LuaScriptHost.h"

#include <filesystem>
#include <string>

#if DEMI_HAS_LUA54
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#endif

namespace demi::runtime {

[[nodiscard]] std::string normalizedKey(std::string key);

#if DEMI_HAS_LUA54
[[nodiscard]] bool luaCall(lua_State* state, int argCount, int resultCount, std::string& error);
void luaReportCallbackError(const char* functionName, const std::filesystem::path& path, const std::string& ownerId, const std::string& error);
[[nodiscard]] std::filesystem::path luaResolveScriptPath(const ProjectData& project, const std::string& module);
void luaConfigurePackagePath(lua_State* state, const ProjectData& project);
[[nodiscard]] std::filesystem::file_time_type luaScriptWriteTime(const std::filesystem::path& path);
[[nodiscard]] bool luaLoadScriptTable(lua_State* state, const std::filesystem::path& scriptPath, std::string& error);
void luaCallLifecycle(lua_State* state, int tableRef, const char* functionName, const std::filesystem::path& path, const std::string& ownerId, float dt = 0.0F);
void luaCallUiEvent(lua_State* state, int tableRef, const char* functionName, const HudButtonElement& button, Vec2 mousePosition, const std::filesystem::path& path);
void luaCallActionEvent(lua_State* state, int tableRef, const std::string& functionName, const HudButtonElement& button, Vec2 mousePosition, const std::filesystem::path& path);
void luaCallModuleActionEvent(lua_State* state, const std::string& moduleName, const std::string& functionName, const HudButtonElement& button, Vec2 mousePosition, const std::filesystem::path& path);
[[nodiscard]] bool luaRegisterBindings(LuaScriptHost& host, lua_State* state, std::string& error);
#endif

} // namespace demi::runtime
