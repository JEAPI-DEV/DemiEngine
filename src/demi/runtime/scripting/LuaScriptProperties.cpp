#include "demi/runtime/scripting/LuaScriptHostInternal.h"
#include "demi/runtime/scripting/ScriptPropertyContract.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <nlohmann/json.hpp>

#include <sol/sol.hpp>

namespace demi::runtime {

bool applyScriptProperties(lua_State *state, const int tableRef,
                           const std::string &propertiesJson,
                           std::string &error,
                           const nlohmann::json *headerSchema) {
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
  nlohmann::json resolved = authored;
  if (headerSchema != nullptr) {
    const auto values = resolveScriptProperties(*headerSchema, authored, error);
    if (!values)
      return false;
    resolved = *values;
  } else {
    const sol::object schemaObject = script["property_schema"];
    if (schemaObject.valid() && schemaObject.get_type() != sol::type::nil) {
      if (schemaObject.get_type() != sol::type::table) {
        error = "property_schema must be a table keyed by property name.";
        return false;
      }
      const auto values = resolveScriptProperties(luaObjectToJson(schemaObject),
                                                  authored, error);
      if (!values)
        return false;
      resolved = *values;
    }
  }

  try {
    for (const auto &[key, value] : resolved.items())
      script[key] = jsonToLuaObject(state, value);
  } catch (const std::exception &exception) {
    error = std::string("Could not apply script properties: ") + exception.what();
    return false;
  }
  return true;
}

} // namespace demi::runtime
