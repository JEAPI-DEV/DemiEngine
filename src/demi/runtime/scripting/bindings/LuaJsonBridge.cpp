#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <algorithm>
#include <vector>

namespace demi::runtime {

nlohmann::json luaObjectToJson(const sol::object object) {
  switch (object.get_type()) {
  case sol::type::boolean:
    return object.as<bool>();
  case sol::type::number:
    return object.as<double>();
  case sol::type::string:
    return object.as<std::string>();
  case sol::type::table: {
    const sol::table table = object.as<sol::table>();
    bool arrayLike = true;
    int maxIndex = 0;
    std::vector<std::pair<int, sol::object>> indexedItems;
    for (const auto& [key, item] : table) {
      if (!key.is<int>() || key.as<int>() <= 0) {
        arrayLike = false;
        break;
      }
      const int index = key.as<int>();
      maxIndex = std::max(maxIndex, index);
      indexedItems.push_back({index, item});
    }
    if (arrayLike && !indexedItems.empty() && maxIndex == static_cast<int>(indexedItems.size())) {
      std::ranges::sort(indexedItems, {}, &std::pair<int, sol::object>::first);
      nlohmann::json value = nlohmann::json::array();
      for (const auto& [_, item] : indexedItems) {
        value.push_back(luaObjectToJson(item));
      }
      return value;
    }

    nlohmann::json value = nlohmann::json::object();
    for (const auto& [key, item] : table) {
      if (key.is<std::string>()) {
        value[key.as<std::string>()] = luaObjectToJson(item);
      } else if (key.is<int>()) {
        value[std::to_string(key.as<int>())] = luaObjectToJson(item);
      }
    }
    return value;
  }
  default:
    return nullptr;
  }
}

sol::object jsonToLuaObject(lua_State* state, const nlohmann::json& value) {
  sol::state_view lua(state);
  if (value.is_object()) {
    sol::table table = lua.create_table();
    for (const auto& [key, item] : value.items()) {
      table.set(key, jsonToLuaObject(state, item));
    }
    return sol::make_object(state, table);
  }
  if (value.is_array()) {
    sol::table table = lua.create_table();
    int index = 1;
    for (const nlohmann::json& item : value) {
      table.set(index++, jsonToLuaObject(state, item));
    }
    return sol::make_object(state, table);
  }
  if (value.is_boolean()) {
    return sol::make_object(state, value.get<bool>());
  }
  if (value.is_number_integer()) {
    return sol::make_object(state, value.get<int>());
  }
  if (value.is_number()) {
    return sol::make_object(state, value.get<double>());
  }
  if (value.is_string()) {
    return sol::make_object(state, value.get<std::string>());
  }
  return sol::nil;
}

std::string encodeNetworkMessage(const std::string& type, const sol::optional<sol::object> payload) {
  nlohmann::json message = nlohmann::json::object();
  message["type"] = type;
  message["payload"] = payload.has_value() ? luaObjectToJson(*payload) : nlohmann::json::object();
  return message.dump();
}

sol::object decodeNetworkMessage(lua_State* state, const std::string& text) {
  try {
    const nlohmann::json message = nlohmann::json::parse(text);
    if (!message.is_object() || !message.contains("type") || !message["type"].is_string()) {
      return sol::nil;
    }
    sol::state_view lua(state);
    sol::table result = lua.create_table();
    result["type"] = message["type"].get<std::string>();
    result["payload"] = jsonToLuaObject(state, message.value("payload", nlohmann::json::object()));
    return sol::make_object(state, result);
  } catch (...) {
    return sol::nil;
  }
}

} // namespace demi::runtime
