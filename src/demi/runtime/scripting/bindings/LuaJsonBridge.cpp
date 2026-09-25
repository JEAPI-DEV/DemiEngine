#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"
#include "demi/runtime/scripting/LuaServiceModules.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace demi::runtime {
namespace {

constexpr std::string_view JsonKindField = "__demi_json_kind";
constexpr int JsonConversionStackSlots = 4;
static_assert(std::numeric_limits<lua_Integer>::is_signed &&
              sizeof(lua_Integer) <= sizeof(std::int64_t));

enum class LuaJsonTableKind { Unspecified, Array, Object, Null };

class LuaStackGuard {
public:
  explicit LuaStackGuard(lua_State *state)
      : state_(state), top_(lua_gettop(state)) {}
  ~LuaStackGuard() { lua_settop(state_, top_); }
  LuaStackGuard(const LuaStackGuard &) = delete;
  LuaStackGuard &operator=(const LuaStackGuard &) = delete;

private:
  lua_State *state_;
  int top_;
};

void reserveJsonConversionStack(lua_State *state) {
  if (lua_checkstack(state, JsonConversionStackSlots) == 0)
    throw std::runtime_error("Lua stack could not grow for JSON conversion");
}

// Registry references keep traversal depth off the Lua value stack.
class LuaRegistryReference {
public:
  static LuaRegistryReference capture(lua_State *state, const int valueIndex) {
    const int absoluteIndex = lua_absindex(state, valueIndex);
    reserveJsonConversionStack(state);
    lua_pushvalue(state, absoluteIndex);
    const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
    if (reference == LUA_NOREF || reference == LUA_REFNIL)
      throw std::runtime_error(
          "Lua registry could not retain a JSON conversion value");
    return LuaRegistryReference(state, reference);
  }

  ~LuaRegistryReference() {
    if (reference_ != LUA_NOREF)
      luaL_unref(state_, LUA_REGISTRYINDEX, reference_);
  }

  LuaRegistryReference(const LuaRegistryReference &) = delete;
  LuaRegistryReference &operator=(const LuaRegistryReference &) = delete;

  LuaRegistryReference(LuaRegistryReference &&other) noexcept
      : state_(other.state_), reference_(other.reference_) {
    other.reference_ = LUA_NOREF;
  }

  LuaRegistryReference &operator=(LuaRegistryReference &&other) = delete;

  void push() const {
    reserveJsonConversionStack(state_);
    lua_rawgeti(state_, LUA_REGISTRYINDEX, reference_);
    if (!lua_istable(state_, -1))
      throw std::runtime_error(
          "Lua registry lost a JSON conversion table");
  }

private:
  LuaRegistryReference(lua_State *state, const int reference)
      : state_(state), reference_(reference) {}

  lua_State *state_;
  int reference_;
};

LuaJsonTableKind tableKind(lua_State *state, const int tableIndex) {
  reserveJsonConversionStack(state);
  if (!lua_getmetatable(state, tableIndex))
    return LuaJsonTableKind::Unspecified;
  lua_pushlstring(state, JsonKindField.data(), JsonKindField.size());
  lua_rawget(state, -2);
  if (lua_isnil(state, -1)) {
    lua_pop(state, 2);
    return LuaJsonTableKind::Unspecified;
  }
  if (!lua_isstring(state, -1))
    throw std::invalid_argument("Lua JSON table kind must be a string");
  const std::string kind = lua_tostring(state, -1);
  lua_pop(state, 2);
  if (kind == "array")
    return LuaJsonTableKind::Array;
  if (kind == "object")
    return LuaJsonTableKind::Object;
  if (kind == "null")
    return LuaJsonTableKind::Null;
  throw std::invalid_argument("Lua JSON table has an unsupported kind");
}

struct LuaTableShape {
  bool hasIntegerKeys = false;
  bool hasStringKeys = false;
  std::size_t size = 0;
  lua_Integer maximumIndex = 0;
  std::vector<std::string> objectKeys;
};

LuaTableShape inspectTable(lua_State *state, const int tableIndex) {
  LuaTableShape shape;
  lua_pushnil(state);
  while (lua_next(state, tableIndex) != 0) {
    ++shape.size;
    if (lua_type(state, -2) == LUA_TSTRING) {
      shape.hasStringKeys = true;
      std::size_t length = 0;
      const char *key = lua_tolstring(state, -2, &length);
      shape.objectKeys.emplace_back(key, length);
    } else if (lua_type(state, -2) == LUA_TNUMBER &&
               lua_isinteger(state, -2)) {
      const lua_Integer index = lua_tointeger(state, -2);
      if (index <= 0)
        throw std::invalid_argument(
            "Lua JSON array keys must be positive integers");
      shape.hasIntegerKeys = true;
      shape.maximumIndex = std::max(shape.maximumIndex, index);
    } else {
      throw std::invalid_argument(
          "Lua JSON object keys must be strings and array keys must be "
          "positive integers");
    }
    lua_pop(state, 1);
  }
  return shape;
}

LuaJsonTableKind resolveTableKind(const LuaJsonTableKind declaredKind,
                                  const LuaTableShape &shape) {
  if (declaredKind == LuaJsonTableKind::Null) {
    if (shape.size != 0)
      throw std::invalid_argument("Data.null must not contain fields");
    return declaredKind;
  }
  if (shape.hasIntegerKeys && shape.hasStringKeys)
    throw std::invalid_argument(
        "Lua JSON tables cannot mix array and object keys");
  if (declaredKind == LuaJsonTableKind::Array && shape.hasStringKeys)
    throw std::invalid_argument("Lua JSON array contains an object key");
  if (declaredKind == LuaJsonTableKind::Object && shape.hasIntegerKeys)
    throw std::invalid_argument("Lua JSON object contains an array key");

  LuaJsonTableKind kind = declaredKind;
  if (kind == LuaJsonTableKind::Unspecified) {
    kind = shape.hasIntegerKeys ? LuaJsonTableKind::Array
                                : LuaJsonTableKind::Object;
  }
  if (kind == LuaJsonTableKind::Array &&
      static_cast<std::uintmax_t>(shape.maximumIndex) != shape.size) {
    throw std::invalid_argument(
        "Lua JSON arrays must use dense one-based integer keys");
  }
  return kind;
}

struct LuaToJsonFrame {
  LuaRegistryReference table;
  const void *identity;
  nlohmann::json *output;
  LuaJsonTableKind kind;
  std::size_t size;
  std::size_t nextIndex = 0;
  std::vector<std::string> objectKeys;
};

nlohmann::json luaPrimitiveToJson(lua_State *state, const int valueIndex) {
  const int absoluteIndex = lua_absindex(state, valueIndex);
  switch (lua_type(state, absoluteIndex)) {
  case LUA_TNIL:
    return nullptr;
  case LUA_TBOOLEAN:
    return lua_toboolean(state, absoluteIndex) != 0;
  case LUA_TNUMBER:
    if (lua_isinteger(state, absoluteIndex))
      return static_cast<std::int64_t>(
          lua_tointeger(state, absoluteIndex));
    if (const lua_Number number = lua_tonumber(state, absoluteIndex);
        std::isfinite(number))
      return static_cast<double>(number);
    throw std::invalid_argument("Lua JSON numbers must be finite");
  case LUA_TSTRING: {
    std::size_t length = 0;
    const char *text = lua_tolstring(state, absoluteIndex, &length);
    return std::string(text, length);
  }
  case LUA_TTABLE:
    throw std::logic_error("Lua table reached primitive JSON conversion");
  default:
    throw std::invalid_argument(
        "Lua JSON values cannot contain functions, userdata, threads, or "
        "unsupported values");
  }
}

std::optional<LuaToJsonFrame> beginLuaTableConversion(
    lua_State *state, const int tableIndex, nlohmann::json &output,
    std::unordered_set<const void *> &activeTables) {
  const int absoluteIndex = lua_absindex(state, tableIndex);
  const void *identity = lua_topointer(state, absoluteIndex);
  if (!activeTables.insert(identity).second)
    throw std::invalid_argument("Lua JSON value contains a table cycle");

  LuaTableShape shape = inspectTable(state, absoluteIndex);
  const LuaJsonTableKind kind =
      resolveTableKind(tableKind(state, absoluteIndex), shape);
  if (kind == LuaJsonTableKind::Null) {
    activeTables.erase(identity);
    output = nullptr;
    return std::nullopt;
  }

  if (kind == LuaJsonTableKind::Array) {
    output = nlohmann::json::array();
    output.get_ref<nlohmann::json::array_t &>().reserve(shape.size);
  } else {
    output = nlohmann::json::object();
  }

  return LuaToJsonFrame{LuaRegistryReference::capture(state, absoluteIndex),
                        identity,
                        &output,
                        kind,
                        shape.size,
                        0,
                        std::move(shape.objectKeys)};
}

nlohmann::json luaValueToJson(lua_State *state, const int valueIndex) {
  const int absoluteIndex = lua_absindex(state, valueIndex);
  if (!lua_istable(state, absoluteIndex))
    return luaPrimitiveToJson(state, absoluteIndex);

  nlohmann::json result;
  std::unordered_set<const void *> activeTables;
  std::optional<LuaToJsonFrame> root = beginLuaTableConversion(
      state, absoluteIndex, result, activeTables);
  if (!root)
    return result;

  std::vector<LuaToJsonFrame> frames;
  frames.push_back(std::move(*root));
  while (!frames.empty()) {
    LuaToJsonFrame &frame = frames.back();
    if (frame.nextIndex == frame.size) {
      activeTables.erase(frame.identity);
      frames.pop_back();
      continue;
    }

    const bool isArray = frame.kind == LuaJsonTableKind::Array;
    const std::size_t itemIndex = frame.nextIndex++;
    std::string objectKey;
    if (!isArray)
      objectKey = frame.objectKeys[itemIndex];

    frame.table.push();
    const int tableIndex = lua_gettop(state);
    if (isArray) {
      if (itemIndex >= static_cast<std::uintmax_t>(LUA_MAXINTEGER))
        throw std::overflow_error("Lua JSON array index exceeds Lua range");
      lua_rawgeti(state, tableIndex,
                  static_cast<lua_Integer>(itemIndex + 1));
    } else {
      lua_pushlstring(state, objectKey.data(), objectKey.size());
      lua_rawget(state, tableIndex);
    }

    nlohmann::json *itemOutput = nullptr;
    if (isArray) {
      frame.output->push_back(nullptr);
      itemOutput = &frame.output->back();
    } else {
      itemOutput = &(*frame.output)[objectKey];
    }

    if (lua_istable(state, -1)) {
      std::optional<LuaToJsonFrame> child = beginLuaTableConversion(
          state, -1, *itemOutput, activeTables);
      lua_pop(state, 2);
      if (child)
        frames.push_back(std::move(*child));
    } else {
      *itemOutput = luaPrimitiveToJson(state, -1);
      lua_pop(state, 2);
    }
  }
  return result;
}

void pushDataNull(lua_State *state) {
  pushLuaService(state, "Data");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    lua_getglobal(state, "Data");
  }
  if (!lua_istable(state, -1))
    throw std::runtime_error("The Lua Data service is not installed");
  lua_getfield(state, -1, "null");
  lua_remove(state, -2);
  if (!lua_istable(state, -1))
    throw std::runtime_error("The Lua Data.null sentinel is not installed");
}

bool pushJsonNode(lua_State *state, const nlohmann::json &value,
                  const bool preserveDataKinds) {
  reserveJsonConversionStack(state);
  if (value.is_null()) {
    if (preserveDataKinds)
      pushDataNull(state);
    else
      lua_pushnil(state);
    return false;
  }
  if (value.is_object()) {
    lua_createtable(state, 0,
                    static_cast<int>(std::min<std::size_t>(
                        value.size(), static_cast<std::size_t>(
                                          std::numeric_limits<int>::max()))));
    if (preserveDataKinds)
      setLuaJsonTableKind(state, "object");
    return true;
  }
  if (value.is_array()) {
    if (value.size() > static_cast<std::uintmax_t>(LUA_MAXINTEGER))
      throw std::overflow_error("JSON array size exceeds Lua integer range");
    lua_createtable(state, static_cast<int>(std::min<std::size_t>(
                                value.size(),
                                static_cast<std::size_t>(
                                    std::numeric_limits<int>::max()))),
                    0);
    if (preserveDataKinds)
      setLuaJsonTableKind(state, "array");
    return true;
  }
  if (value.is_boolean()) {
    lua_pushboolean(state, value.get<bool>());
    return false;
  }
  if (value.is_number_unsigned()) {
    const std::uint64_t integer = value.get<std::uint64_t>();
    if (integer > static_cast<std::uintmax_t>(LUA_MAXINTEGER))
      throw std::overflow_error("JSON unsigned integer exceeds Lua range");
    lua_pushinteger(state, static_cast<lua_Integer>(integer));
    return false;
  }
  if (value.is_number_integer()) {
    const std::int64_t integer = value.get<std::int64_t>();
    if (integer < static_cast<std::intmax_t>(LUA_MININTEGER) ||
        integer > static_cast<std::intmax_t>(LUA_MAXINTEGER))
      throw std::overflow_error("JSON integer exceeds Lua integer range");
    lua_pushinteger(state, static_cast<lua_Integer>(integer));
    return false;
  }
  if (value.is_number_float()) {
    const double number = value.get<double>();
    if (!std::isfinite(number))
      throw std::invalid_argument("JSON numbers must be finite");
    const lua_Number luaNumber = static_cast<lua_Number>(number);
    if (!std::isfinite(luaNumber))
      throw std::overflow_error("JSON number exceeds Lua number range");
    lua_pushnumber(state, luaNumber);
    return false;
  }
  if (value.is_string()) {
    const auto &text = value.get_ref<const std::string &>();
    lua_pushlstring(state, text.data(), text.size());
    return false;
  }
  throw std::invalid_argument("JSON value cannot be represented in Lua");
}

struct JsonToLuaFrame {
  JsonToLuaFrame(const nlohmann::json &inputValue,
                 LuaRegistryReference outputTable)
      : input(&inputValue), next(inputValue.cbegin()), end(inputValue.cend()),
        table(std::move(outputTable)) {}

  const nlohmann::json *input;
  nlohmann::json::const_iterator next;
  nlohmann::json::const_iterator end;
  std::size_t nextArrayIndex = 1;
  LuaRegistryReference table;
};

void pushJsonValue(lua_State *state, const nlohmann::json &value,
                   const bool preserveDataKinds) {
  if (!pushJsonNode(state, value, preserveDataKinds))
    return;

  std::vector<JsonToLuaFrame> frames;
  frames.emplace_back(value, LuaRegistryReference::capture(state, -1));
  while (!frames.empty()) {
    JsonToLuaFrame &frame = frames.back();
    if (frame.next == frame.end) {
      frames.pop_back();
      continue;
    }

    const nlohmann::json &item = *frame.next;
    const bool isObject = frame.input->is_object();
    std::string objectKey;
    std::size_t arrayIndex = 0;
    if (isObject) {
      objectKey = frame.next.key();
    } else {
      arrayIndex = frame.nextArrayIndex++;
    }
    ++frame.next;

    frame.table.push();
    const int tableIndex = lua_gettop(state);
    if (isObject)
      lua_pushlstring(state, objectKey.data(), objectKey.size());
    const bool itemIsContainer =
        pushJsonNode(state, item, preserveDataKinds);
    std::optional<LuaRegistryReference> childTable;
    if (itemIsContainer)
      childTable.emplace(LuaRegistryReference::capture(state, -1));

    if (isObject) {
      lua_rawset(state, tableIndex);
    } else {
      lua_rawseti(state, tableIndex,
                  static_cast<lua_Integer>(arrayIndex));
    }
    lua_pop(state, 1);

    if (childTable)
      frames.emplace_back(item, std::move(*childTable));
  }
}

sol::object jsonToLua(lua_State *state, const nlohmann::json &value,
                      const bool preserveDataKinds) {
  LuaStackGuard stack(state);
  pushJsonValue(state, value, preserveDataKinds);
  return sol::stack::get<sol::object>(state, -1);
}

} // namespace

nlohmann::json luaObjectToJson(const sol::object object) {
  if (!object.valid())
    return nullptr;
  lua_State *state = object.lua_state();
  LuaStackGuard stack(state);
  reserveJsonConversionStack(state);
  object.push();
  return luaValueToJson(state, -1);
}

sol::object jsonToLuaObject(lua_State *state, const nlohmann::json &value) {
  return jsonToLua(state, value, false);
}

sol::object jsonToLuaDataObject(lua_State *state,
                                const nlohmann::json &value) {
  return jsonToLua(state, value, true);
}

void setLuaJsonTableKind(lua_State *state, const std::string_view kind) {
  if (kind != "array" && kind != "object" && kind != "null")
    throw std::invalid_argument("Unsupported Lua JSON table kind");
  reserveJsonConversionStack(state);
  lua_newtable(state);
  lua_pushlstring(state, kind.data(), kind.size());
  lua_setfield(state, -2, JsonKindField.data());
  lua_pushboolean(state, false);
  lua_setfield(state, -2, "__metatable");
  lua_setmetatable(state, -2);
}

std::string
encodeNetworkMessage(const std::string &type,
                     const sol::optional<sol::object> payload) {
  nlohmann::json message = nlohmann::json::object();
  message["type"] = type;
  message["payload"] = payload.has_value() ? luaObjectToJson(*payload)
                                           : nlohmann::json::object();
  return message.dump();
}

sol::object decodeNetworkMessage(lua_State *state, const std::string &text) {
  try {
    const nlohmann::json message = nlohmann::json::parse(text);
    if (!message.is_object() || !message.contains("type") ||
        !message["type"].is_string()) {
      return sol::nil;
    }
    sol::state_view lua(state);
    sol::table result = lua.create_table();
    result["type"] = message["type"].get<std::string>();
    result["payload"] = jsonToLuaObject(
        state, message.value("payload", nlohmann::json::object()));
    return sol::make_object(state, result);
  } catch (...) {
    return sol::nil;
  }
}

} // namespace demi::runtime
