#include "demi/runtime/scripting/LuaTransfer.h"
#include "demi/runtime/scripting/LuaSharedMap.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"
#include "demi/runtime/scripting/lua_compat/lua.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace demi::runtime {
namespace {
using Value = LuaTransferGraph::Value;
using Marker = LuaTransferGraph::Table::Marker;
constexpr std::string_view KindField = "__demi_json_kind";
static_assert(std::numeric_limits<lua_Integer>::is_signed &&
              sizeof(lua_Integer) == sizeof(std::int64_t));

class StackGuard {
public:
  explicit StackGuard(lua_State *state)
      : state_(state), top_(lua_gettop(state)) {}
  ~StackGuard() { lua_settop(state_, top_); }
  void commit() { top_ = lua_gettop(state_); }

private:
  lua_State *state_;
  int top_;
};

void reserveStack(lua_State *state, int slots) {
  if (!lua_checkstack(state, slots))
    throw std::invalid_argument("Lua transfer cannot reserve stack space");
}

// One registry root retains all tables without one Lua stack slot per node.
class RegistryTables {
public:
  explicit RegistryTables(lua_State *state) : state_(state) {
    lua_newtable(state_);
    reference_ = luaL_ref(state_, LUA_REGISTRYINDEX);
  }
  ~RegistryTables() { luaL_unref(state_, LUA_REGISTRYINDEX, reference_); }
  RegistryTables(const RegistryTables &) = delete;
  RegistryTables &operator=(const RegistryTables &) = delete;
  void store(std::size_t index, int valueIndex) {
    const int absolute = lua_absindex(state_, valueIndex);
    lua_rawgeti(state_, LUA_REGISTRYINDEX, reference_);
    lua_pushvalue(state_, absolute);
    lua_rawseti(state_, -2, static_cast<lua_Integer>(index + 1));
    lua_pop(state_, 1);
  }
  void push(std::size_t index) const {
    lua_rawgeti(state_, LUA_REGISTRYINDEX, reference_);
    lua_rawgeti(state_, -1, static_cast<lua_Integer>(index + 1));
    lua_remove(state_, -2);
  }

private:
  lua_State *state_;
  int reference_;
};

Marker tableMarker(lua_State *state, int index) {
  StackGuard guard(state);
  if (!lua_getmetatable(state, index))
    return Marker::None;
  const int metatable = lua_gettop(state);
  Marker marker = Marker::None;
  lua_pushnil(state);
  while (lua_next(state, metatable)) {
    if (lua_type(state, -2) != LUA_TSTRING)
      throw std::invalid_argument("Lua transfer rejects unknown metatables");
    std::size_t length = 0;
    const char *text = lua_tolstring(state, -2, &length);
    const std::string_view key(text, length);
    if (key == KindField && lua_type(state, -1) == LUA_TSTRING) {
      text = lua_tolstring(state, -1, &length);
      const std::string_view kind(text, length);
      if (kind == "array")
        marker = Marker::Array;
      else if (kind == "object")
        marker = Marker::Object;
      else if (kind == "null")
        marker = Marker::Null;
      else
        throw std::invalid_argument(
            "Lua transfer rejects unknown Data markers");
    } else if (key != "__metatable" || lua_type(state, -1) != LUA_TBOOLEAN ||
               lua_toboolean(state, -1)) {
      throw std::invalid_argument(
          "Lua transfer requires marker-only metatables");
    }
    lua_pop(state, 1);
  }
  if (marker == Marker::None)
    throw std::invalid_argument("Lua transfer rejects unknown metatables");
  return marker;
}

void validateValue(const Value &value, std::size_t tableCount, bool key) {
  switch (value.kind) {
  case Value::Kind::Nil:
    if (key)
      throw std::invalid_argument("Lua transfer table key cannot be nil");
    break;
  case Value::Kind::Number:
    if (key && std::isnan(value.number))
      throw std::invalid_argument("Lua transfer table key cannot be NaN");
    break;
  case Value::Kind::Table:
    if (value.table >= tableCount)
      throw std::invalid_argument(
          "Lua transfer table reference is out of range");
    break;
  case Value::Kind::Shared:
    if (!value.shared)
      throw std::invalid_argument("Lua transfer SharedMap is null");
    break;
  case Value::Kind::Boolean:
  case Value::Kind::Integer:
  case Value::Kind::String:
    break;
  default:
    throw std::invalid_argument("Lua transfer value kind is invalid");
  }
}
} // namespace

LuaTransferGraph captureLuaValues(lua_State *state, int first, int count,
                                  bool allowShared) {
  if (!state || count < 0)
    throw std::invalid_argument(
        "Lua transfer requires a state and nonnegative count");
  const int top = lua_gettop(state);
  if (count && (first == 0 || first <= LUA_REGISTRYINDEX))
    throw std::invalid_argument("Lua transfer requires a stack value range");
  first = lua_absindex(state, first);
  if (count && (first < 1 || first > top || count > top - first + 1))
    throw std::invalid_argument("Lua transfer root range is out of bounds");
  StackGuard guard(state);
  reserveStack(state, 12);
  RegistryTables retained(state);
  LuaTransferGraph graph;
  graph.roots.reserve(static_cast<std::size_t>(count));
  std::unordered_map<const void *, std::size_t> identities;
  const auto capture = [&](int index) {
    Value value;
    switch (lua_type(state, index)) {
    case LUA_TNIL:
      break;
    case LUA_TBOOLEAN:
      value.kind = Value::Kind::Boolean;
      value.boolean = lua_toboolean(state, index) != 0;
      break;
    case LUA_TNUMBER:
      if (lua_isinteger(state, index)) {
        value.kind = Value::Kind::Integer;
        value.integer = lua_tointeger(state, index);
      } else {
        value.kind = Value::Kind::Number;
        value.number = lua_tonumber(state, index);
      }
      break;
    case LUA_TSTRING: {
      value.kind = Value::Kind::String;
      std::size_t length = 0;
      const char *text = lua_tolstring(state, index, &length);
      value.string.assign(text, length);
      break;
    }
    case LUA_TTABLE: {
      value.kind = Value::Kind::Table;
      const void *identity = lua_topointer(state, index);
      const auto found = identities.find(identity);
      if (found != identities.end()) {
        value.table = found->second;
        break;
      }
      const Marker marker = tableMarker(state, index);
      value.table = graph.tables.size();
      identities.emplace(identity, value.table);
      graph.tables.push_back({marker, {}});
      retained.store(value.table, index);
      break;
    }
    case LUA_TUSERDATA:
      value.shared = luaSharedMap(state, index);
      if (!value.shared)
        throw std::invalid_argument("Lua transfer rejects plain userdata");
      if (!allowShared)
        throw std::invalid_argument(
            "SharedMap storage cannot contain SharedMaps");
      value.kind = Value::Kind::Shared;
      break;
    default:
      throw std::invalid_argument(
          "Lua transfer rejects functions, threads and native values");
    }
    return value;
  };
  for (int root = 0; root < count; ++root)
    graph.roots.push_back(capture(first + root));
  for (std::size_t table = 0; table < graph.tables.size(); ++table) {
    retained.push(table);
    const int index = lua_gettop(state);
    lua_pushnil(state);
    while (lua_next(state, index)) {
      auto key = capture(-2);
      auto value = capture(-1);
      // Capture can grow graph.tables: never retain a reference across it.
      graph.tables[table].entries.emplace_back(std::move(key),
                                               std::move(value));
      lua_pop(state, 1);
    }
    lua_pop(state, 1);
  }
  return graph;
}

void pushLuaValues(lua_State *state, const LuaTransferGraph &graph) {
  if (!state)
    throw std::invalid_argument("Lua transfer requires a state");
  if (graph.roots.size() >
      static_cast<std::size_t>(std::numeric_limits<int>::max() - 12))
    throw std::invalid_argument(
        "Lua transfer has too many roots for the Lua stack");
  for (const auto &root : graph.roots)
    validateValue(root, graph.tables.size(), false);
  for (const auto &table : graph.tables) {
    switch (table.marker) {
    case Marker::None:
    case Marker::Array:
    case Marker::Object:
    case Marker::Null:
      break;
    default:
      throw std::invalid_argument("Lua transfer table marker is invalid");
    }
    for (const auto &[key, value] : table.entries) {
      validateValue(key, graph.tables.size(), true);
      validateValue(value, graph.tables.size(), false);
    }
  }
  StackGuard guard(state);
  reserveStack(state, static_cast<int>(graph.roots.size()) + 12);
  RegistryTables retained(state);
  for (std::size_t index = 0; index < graph.tables.size(); ++index) {
    lua_newtable(state);
    const auto marker = graph.tables[index].marker;
    if (marker != Marker::None)
      setLuaJsonTableKind(state, marker == Marker::Array    ? "array"
                                 : marker == Marker::Object ? "object"
                                                            : "null");
    retained.store(index, -1);
    lua_pop(state, 1);
  }
  std::unordered_map<LuaSharedMap *, std::size_t> sharedIdentities;
  const auto push = [&](const Value &value) {
    switch (value.kind) {
    case Value::Kind::Nil:
      lua_pushnil(state);
      break;
    case Value::Kind::Boolean:
      lua_pushboolean(state, value.boolean);
      break;
    case Value::Kind::Integer:
      lua_pushinteger(state, value.integer);
      break;
    case Value::Kind::Number:
      lua_pushnumber(state, value.number);
      break;
    case Value::Kind::String:
      lua_pushlstring(state, value.string.data(), value.string.size());
      break;
    case Value::Kind::Table:
      retained.push(value.table);
      break;
    case Value::Kind::Shared: {
      const auto found = sharedIdentities.find(value.shared.get());
      if (found != sharedIdentities.end()) {
        retained.push(found->second);
      } else {
        const std::size_t index = graph.tables.size() + sharedIdentities.size();
        pushLuaSharedMap(state, value.shared);
        retained.store(index, -1);
        sharedIdentities.emplace(value.shared.get(), index);
      }
      break;
    }
    }
  };
  for (std::size_t index = 0; index < graph.tables.size(); ++index) {
    retained.push(index);
    for (const auto &[key, value] : graph.tables[index].entries) {
      push(key);
      push(value);
      lua_rawset(state, -3);
    }
    lua_pop(state, 1);
  }
  for (const auto &root : graph.roots)
    push(root);
  guard.commit();
}

} // namespace demi::runtime
