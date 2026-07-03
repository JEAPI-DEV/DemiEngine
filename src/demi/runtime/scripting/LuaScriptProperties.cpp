#include "demi/runtime/scripting/LuaScriptHostInternal.h"

#include <nlohmann/json.hpp>

namespace demi::runtime {

namespace {

void pushJsonToLua(lua_State* state, const nlohmann::json& value) {
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
    for (const nlohmann::json& item : value) {
      pushJsonToLua(state, item);
      lua_rawseti(state, -2, index++);
    }
    return;
  }
  if (value.is_object()) {
    lua_newtable(state);
    for (const auto& [key, item] : value.items()) {
      pushJsonToLua(state, item);
      lua_setfield(state, -2, key.c_str());
    }
    return;
  }
  lua_pushnil(state);
}

} // namespace

void applyScriptProperties(lua_State* state, const int tableRef, const std::string& propertiesJson) {
  if (propertiesJson.empty()) {
    return;
  }

  nlohmann::json properties;
  try {
    properties = nlohmann::json::parse(propertiesJson);
  } catch (...) {
    return;
  }
  if (!properties.is_object()) {
    return;
  }

  lua_rawgeti(state, LUA_REGISTRYINDEX, tableRef);
  for (const auto& [key, value] : properties.items()) {
    pushJsonToLua(state, value);
    lua_setfield(state, -2, key.c_str());
  }
  lua_pop(state, 1);
}

} // namespace demi::runtime
