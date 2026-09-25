#pragma once

#include <sol/sol.hpp>

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace demi::runtime {

nlohmann::json luaObjectToJson(sol::object object);
sol::object jsonToLuaObject(lua_State *state, const nlohmann::json &value);
sol::object jsonToLuaDataObject(lua_State *state,
                                const nlohmann::json &value);
void setLuaJsonTableKind(lua_State *state, std::string_view kind);
std::string encodeNetworkMessage(const std::string &type,
                                 sol::optional<sol::object> payload);
sol::object decodeNetworkMessage(lua_State *state, const std::string &text);

} // namespace demi::runtime
