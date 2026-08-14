#include "demi/runtime/scripting/LuaScriptHostInternal.h"
#include "demi/runtime/scripting/ScriptPropertyContract.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <nlohmann/json.hpp>

#include <sol/sol.hpp>

namespace demi::runtime {

namespace {

void pushJsonToLua(lua_State *state, const nlohmann::json &value) {
  if (value.is_boolean()) {
    lua_pushboolean(state, value.get<bool>() ? 1 : 0);
    return;
  }
  if (value.is_number()) {
    lua_pushnumber(state, value.get<double>());
    return;
  }
  if (value.is_string()) {
    const std::string text = value.get<std::string>();
    lua_pushlstring(state, text.c_str(), text.size());
    return;
  }
  if (value.is_array()) {
    lua_newtable(state);
    int index = 1;
    for (const nlohmann::json &item : value) {
      pushJsonToLua(state, item);
      lua_rawseti(state, -2, index++);
    }
    return;
  }
  if (value.is_object()) {
    lua_newtable(state);
    for (const auto &[key, item] : value.items()) {
      pushJsonToLua(state, item);
      lua_setfield(state, -2, key.c_str());
    }
    return;
  }
  lua_pushnil(state);
}

} // namespace

bool applyScriptProperties(lua_State *state, const int tableRef,
                           const std::string &propertiesJson,
                           std::string &error) {
  nlohmann::json authored = nlohmann::json::object();
  try {
    if (!propertiesJson.empty())
      authored = nlohmann::json::parse(propertiesJson);
  } catch (const std::exception &exception) {
    error = std::string("LuaScript properties are invalid JSON: ") +
            exception.what();
    return false;
  }
  if (!authored.is_object()) {
    error = "LuaScript properties must be an object.";
    return false;
  }

  sol::state_view lua(state);
  sol::table script = lua.registry()[tableRef];
  const sol::object schemaObject = script["property_schema"];
  nlohmann::json resolved = authored;
  if (schemaObject.valid() && schemaObject.get_type() != sol::type::nil) {
    if (schemaObject.get_type() != sol::type::table) {
      error = "property_schema must be a table keyed by property name.";
      return false;
    }
    const auto values =
        resolveScriptProperties(luaObjectToJson(schemaObject), authored, error);
    if (!values)
      return false;
    resolved = *values;
  }

  lua_rawgeti(state, LUA_REGISTRYINDEX, tableRef);
  for (const auto &[key, value] : resolved.items()) {
    pushJsonToLua(state, value);
    lua_setfield(state, -2, key.c_str());
  }
  lua_pop(state, 1);
  return true;
}

} // namespace demi::runtime
