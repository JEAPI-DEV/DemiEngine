#pragma once

#include <sol/sol.hpp>

#include <string>

#include <nlohmann/json.hpp>

namespace demi::runtime {

nlohmann::json luaObjectToJson(sol::object object);
sol::object jsonToLuaObject(lua_State* state, const nlohmann::json& value);
std::string encodeNetworkMessage(const std::string& type, sol::optional<sol::object> payload);
sol::object decodeNetworkMessage(lua_State* state, const std::string& text);

} // namespace demi::runtime
