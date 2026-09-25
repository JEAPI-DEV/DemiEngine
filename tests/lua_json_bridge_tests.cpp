#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"
#include "demi/runtime/scripting/LuaServiceModules.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using demi::runtime::jsonToLuaDataObject;
using demi::runtime::jsonToLuaObject;
using demi::runtime::luaObjectToJson;
using demi::runtime::setLuaJsonTableKind;

void markTable(sol::table table, const std::string_view kind) {
  lua_State *state = table.lua_state();
  table.push();
  setLuaJsonTableKind(state, kind);
  lua_pop(state, 1);
}

std::string tableKind(const sol::object &object) {
  lua_State *state = object.lua_state();
  const int top = lua_gettop(state);
  object.push();
  if (!lua_getmetatable(state, -1)) {
    lua_settop(state, top);
    return {};
  }
  lua_getfield(state, -1, "__demi_json_kind");
  const char *kind = lua_tostring(state, -1);
  const std::string result = kind != nullptr ? kind : "";
  lua_settop(state, top);
  return result;
}

bool sameLuaObject(const sol::object &left, const sol::object &right) {
  lua_State *state = left.lua_state();
  const int top = lua_gettop(state);
  left.push();
  right.push();
  const bool same = lua_rawequal(state, -1, -2) != 0;
  lua_settop(state, top);
  return same;
}

template <typename Exception, typename Function>
bool throws(Function function) {
  try {
    function();
  } catch (const Exception &) {
    return true;
  } catch (...) {
  }
  return false;
}

} // namespace

int main() {
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::package);

  sol::table data = lua.create_table();
  sol::table dataNull = lua.create_table();
  markTable(dataNull, "null");
  data["null"] = dataNull;
  lua["Data"] = data;
  demi::runtime::publishLuaServiceModules(lua.lua_state());

  constexpr lua_Integer wideInteger = 9007199254740993LL;
  sol::table array = lua.create_table();
  array[1] = "first";
  array[2] = wideInteger;
  const nlohmann::json encodedArray = luaObjectToJson(array);
  if (!encodedArray.is_array() || encodedArray.size() != 2 ||
      encodedArray[0] != "first" || !encodedArray[1].is_number_integer() ||
      encodedArray[1].get<std::int64_t>() != wideInteger) {
    std::cerr << "Lua JSON bridge did not preserve a dense array or integer "
                 "width.\n";
    return 1;
  }

  sol::table emptyArray = lua.create_table();
  sol::table emptyObject = lua.create_table();
  markTable(emptyArray, "array");
  markTable(emptyObject, "object");
  if (!luaObjectToJson(emptyArray).is_array() ||
      !luaObjectToJson(emptyArray).empty() ||
      !luaObjectToJson(emptyObject).is_object() ||
      !luaObjectToJson(emptyObject).empty() ||
      !luaObjectToJson(dataNull).is_null()) {
    std::cerr << "Lua JSON bridge ignored Data table-kind metadata.\n";
    return 1;
  }

  sol::table cycle = lua.create_table();
  cycle["self"] = cycle;
  sol::table mixed = lua.create_table();
  mixed[1] = "array";
  mixed["field"] = "object";
  sol::table sparse = lua.create_table();
  sparse[2] = "gap";
  lua["unsupported_function"] = [] {};
  const sol::object function = lua["unsupported_function"];
  lua_newuserdatauv(lua.lua_state(), 1, 0);
  const sol::object userdata(lua.lua_state(), -1);
  lua_pop(lua.lua_state(), 1);
  const sol::object nonFinite =
      sol::make_object(lua, std::numeric_limits<double>::infinity());
  if (!throws<std::invalid_argument>([&] { luaObjectToJson(mixed); }) ||
      !throws<std::invalid_argument>([&] { luaObjectToJson(sparse); }) ||
      !throws<std::invalid_argument>([&] { luaObjectToJson(function); }) ||
      !throws<std::invalid_argument>([&] { luaObjectToJson(userdata); }) ||
      !throws<std::invalid_argument>([&] { luaObjectToJson(nonFinite); })) {
    std::cerr << "Lua JSON bridge accepted an unrepresentable value.\n";
    return 1;
  }

  const nlohmann::json document = {
      {"null_value", nullptr},
      {"empty_array", nlohmann::json::array()},
      {"empty_object", nlohmann::json::object()},
      {"values", nlohmann::json::array({wideInteger, nullptr})}};
  const sol::object decoded = jsonToLuaDataObject(lua.lua_state(), document);
  const sol::table decodedTable = decoded.as<sol::table>();
  const sol::object decodedNull =
      decodedTable.raw_get<sol::object>("null_value");
  const sol::object decodedEmptyArray =
      decodedTable.raw_get<sol::object>("empty_array");
  const sol::object decodedEmptyObject =
      decodedTable.raw_get<sol::object>("empty_object");
  if (!sameLuaObject(decodedNull, dataNull) ||
      tableKind(decodedEmptyArray) != "array" ||
      tableKind(decodedEmptyObject) != "object" ||
      luaObjectToJson(decoded) != document) {
    std::cerr << "Data-aware JSON decoding did not preserve null or table "
                 "kinds.\n";
    return 1;
  }

  const sol::object defaultNull =
      jsonToLuaObject(lua.lua_state(), nlohmann::json(nullptr));
  if (defaultNull.get_type() != sol::type::nil) {
    std::cerr << "Default JSON decoding no longer maps null to nil.\n";
    return 1;
  }
  const sol::object decodedWide =
      jsonToLuaObject(lua.lua_state(), nlohmann::json(wideInteger));
  decodedWide.push();
  const bool preservedWideInteger = lua_isinteger(lua.lua_state(), -1) &&
                                    lua_tointeger(lua.lua_state(), -1) ==
                                        wideInteger;
  lua_pop(lua.lua_state(), 1);
  if (!preservedWideInteger) {
    std::cerr << "Default JSON decoding truncated a wide integer.\n";
    return 1;
  }

  const std::string binaryKey("binary\0key", 10);
  nlohmann::json binaryKeyDocument = nlohmann::json::object();
  binaryKeyDocument[binaryKey] = "preserved";
  const sol::table decodedBinaryKey =
      jsonToLuaDataObject(lua.lua_state(), binaryKeyDocument).as<sol::table>();
  const sol::object binaryKeyValue =
      decodedBinaryKey.raw_get<sol::object>(binaryKey);
  const nlohmann::json encodedBinaryKey = luaObjectToJson(decodedBinaryKey);
  if (!binaryKeyValue.is<std::string>() ||
      binaryKeyValue.as<std::string>() != "preserved" ||
      !encodedBinaryKey.contains(binaryKey) ||
      encodedBinaryKey.at(binaryKey) != "preserved") {
    std::cerr << "Lua JSON bridge did not preserve a binary object key.\n";
    return 1;
  }

  const nlohmann::json defaultNullDocument = {
      {"null_value", nullptr},
      {"values", nlohmann::json::array({1, nullptr, 3})}};
  const sol::table decodedDefaultNulls =
      jsonToLuaObject(lua.lua_state(), defaultNullDocument).as<sol::table>();
  const sol::table decodedDefaultNullArray =
      decodedDefaultNulls.raw_get<sol::table>("values");
  if (decodedDefaultNulls.raw_get<sol::object>("null_value").get_type() !=
          sol::type::nil ||
      decodedDefaultNullArray.raw_get<int>(1) != 1 ||
      decodedDefaultNullArray.raw_get<sol::object>(2).get_type() !=
          sol::type::nil ||
      decodedDefaultNullArray.raw_get<int>(3) != 3) {
    std::cerr << "Default JSON decoding no longer inserts null as nil.\n";
    return 1;
  }

  constexpr int DeepNesting = 5000;
  nlohmann::json deepJson = nlohmann::json::object();
  nlohmann::json *deepJsonCursor = &deepJson;
  for (int depth = 0; depth < DeepNesting; ++depth) {
    (*deepJsonCursor)["child"] = nlohmann::json::object();
    deepJsonCursor = &(*deepJsonCursor)["child"];
  }
  (*deepJsonCursor)["value"] = "bottom";

  lua_pushliteral(lua.lua_state(), "json stack sentinel");
  const int stackBeforeDeepConversion = lua_gettop(lua.lua_state());
  const auto stackIsPreserved = [&] {
    return lua_gettop(lua.lua_state()) == stackBeforeDeepConversion &&
           lua_isstring(lua.lua_state(), -1) &&
           std::string_view(lua_tostring(lua.lua_state(), -1)) ==
               "json stack sentinel";
  };
  const sol::object deepDecoded =
      jsonToLuaDataObject(lua.lua_state(), deepJson);
  const bool decodingPreservedStack = stackIsPreserved();
  deepDecoded.push();
  bool decodedLeafIsValid = true;
  for (int depth = 0; depth < DeepNesting; ++depth) {
    if (!lua_istable(lua.lua_state(), -1)) {
      decodedLeafIsValid = false;
      break;
    }
    lua_getfield(lua.lua_state(), -1, "child");
    lua_remove(lua.lua_state(), -2);
  }
  if (decodedLeafIsValid && lua_istable(lua.lua_state(), -1)) {
    lua_getfield(lua.lua_state(), -1, "value");
    decodedLeafIsValid =
        lua_isstring(lua.lua_state(), -1) &&
        std::string_view(lua_tostring(lua.lua_state(), -1)) == "bottom";
  } else {
    decodedLeafIsValid = false;
  }
  lua_settop(lua.lua_state(), stackBeforeDeepConversion);

  const nlohmann::json deepEncoded = luaObjectToJson(deepDecoded);
  const bool encodingPreservedStack = stackIsPreserved();
  const nlohmann::json *deepEncodedCursor = &deepEncoded;
  bool encodedLeafIsValid = true;
  for (int depth = 0; depth < DeepNesting; ++depth) {
    if (!deepEncodedCursor->is_object()) {
      encodedLeafIsValid = false;
      break;
    }
    const auto child = deepEncodedCursor->find("child");
    if (child == deepEncodedCursor->end()) {
      encodedLeafIsValid = false;
      break;
    }
    deepEncodedCursor = &*child;
  }
  encodedLeafIsValid =
      encodedLeafIsValid && deepEncodedCursor->is_object() &&
      deepEncodedCursor->value("value", std::string()) == "bottom";

  const bool cycleRejected =
      throws<std::invalid_argument>([&] { (void)luaObjectToJson(cycle); });
  const bool cyclePreservedStack = stackIsPreserved();
  if (!decodingPreservedStack || !decodedLeafIsValid ||
      !encodingPreservedStack || !encodedLeafIsValid || !cycleRejected ||
      !cyclePreservedStack || !stackIsPreserved()) {
    std::cerr << "Deep JSON conversion did not grow and restore the Lua "
                 "stack safely or reject a table cycle.\n";
    return 1;
  }
  const nlohmann::json unsignedOverflow =
      std::numeric_limits<std::uint64_t>::max();
  if (!throws<std::overflow_error>([&] {
        (void)jsonToLuaObject(lua.lua_state(), unsignedOverflow);
      }) ||
      !stackIsPreserved() ||
      !throws<std::overflow_error>([&] {
        (void)jsonToLuaDataObject(lua.lua_state(), unsignedOverflow);
      }) ||
      !stackIsPreserved()) {
    std::cerr << "JSON integer overflow was not rejected with the Lua stack "
                 "restored.\n";
    return 1;
  }
  lua_pop(lua.lua_state(), 1);

  return 0;
}
