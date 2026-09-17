#pragma once
#include <string>
#include <string_view>
struct lua_State;
namespace demi::runtime {
inline constexpr const char *LuaServicesRegistry = "demi.native_services";
std::string luaServiceModuleName(std::string_view service);
void publishLuaServiceModules(lua_State *state);
void pushLuaService(lua_State *state, const char *service);
void clearLuaServiceModules(lua_State *state);
} // namespace demi::runtime
