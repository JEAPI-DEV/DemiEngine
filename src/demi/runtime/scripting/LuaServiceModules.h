#pragma once
#include <string>
#include <string_view>
#include <span>
struct lua_State;
namespace demi::runtime {
inline constexpr const char *LuaServicesRegistry = "demi.native_services";
struct LuaServiceModule {
  std::string_view service;
  std::string_view module;
};
// Explicit public inventory; internal binding names never infer import paths.
std::span<const LuaServiceModule> luaServiceModules();
std::string luaServiceModuleName(std::string_view service);
void publishLuaServiceModules(lua_State *state);
void pushLuaService(lua_State *state, const char *service);
void clearLuaServiceModules(lua_State *state);
} // namespace demi::runtime
