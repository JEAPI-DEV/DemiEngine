#pragma once

#include <sol/sol.hpp>

#include <string>

#include <nlohmann/json.hpp>

namespace demi::runtime {

#if DEMI_HAS_LUA54
nlohmann::json luaObjectToJson(sol::object object);
sol::object jsonToLuaObject(lua_State* state, const nlohmann::json& value);
std::string encodeNetworkMessage(const std::string& type, sol::optional<sol::object> payload);
sol::object decodeNetworkMessage(lua_State* state, const std::string& text);
#endif

} // namespace demi::runtime
