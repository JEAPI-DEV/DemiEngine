#pragma once

#include "demi/runtime/scripting/LuaScriptHostInternal.h"

#include <sol/sol.hpp>

#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

namespace demi::runtime {

#if DEMI_HAS_LUA54
std::tuple<sol::object, sol::object> luaVec2Result(lua_State* state, std::optional<Vec2> value);
std::tuple<sol::object, sol::object, sol::object> luaVec3Result(lua_State* state, std::optional<Vec3> value);
Vec2 luaVec2Field(sol::table table, const char* fieldName, Vec2 fallback = {});
Vec3 luaVec3Field(sol::table table, const char* fieldName, Vec3 fallback = {});
Color luaColorField(sol::table table, const char* fieldName, Color fallback = {1.0F, 1.0F, 1.0F, 1.0F});
Entity luaParseEntitySpec(const std::string& entityId, sol::table spec);
std::uint64_t luaAddTimer(lua_State* state, LuaScriptHost& host, float seconds, bool repeating, sol::function callback);
std::uint64_t luaAddEventSubscription(lua_State* state, LuaScriptHost& host, const std::string& eventName, sol::function callback);
int luaEmitEvent(lua_State* state, LuaScriptHost& host, const std::string& eventName, sol::object payload);
sol::object luaReadSaveTable(lua_State* state, LuaScriptHost& host, const std::string& slot);
bool luaWriteSaveTable(LuaScriptHost& host, const std::string& slot, sol::table table, sol::optional<int> version);
std::uint64_t luaRegisterSaveMigration(lua_State* state, LuaScriptHost& host, int fromVersion, int toVersion, sol::function callback);
PhysicsContactFilter2D luaContactFilterFromTable(sol::optional<sol::table> filterTable);
sol::table luaContactsTable(lua_State* state, const std::vector<PhysicsContact2D>& contacts);
#endif

} // namespace demi::runtime
