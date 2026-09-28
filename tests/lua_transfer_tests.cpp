#include "demi/runtime/scripting/LuaSharedMap.h"
#include "demi/runtime/scripting/LuaTransfer.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

using namespace demi::runtime;

namespace {
using State = std::unique_ptr<lua_State, decltype(&lua_close)>;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void script(lua_State *state, const char *source) {
  if (luaL_dostring(state, source) != LUA_OK)
    throw std::runtime_error(lua_tostring(state, -1));
}

template <typename Function> void rejects(lua_State *state, Function function) {
  const int top = lua_gettop(state);
  try {
    function();
  } catch (const std::invalid_argument &) {
    require(lua_gettop(state) == top, "Rejection changed the Lua stack");
    return;
  }
  throw std::runtime_error("Expected invalid_argument");
}

void scalarRoots(lua_State *source, lua_State *destination) {
  lua_pushnil(source);
  lua_pushboolean(source, true);
  lua_pushboolean(source, false);
  lua_pushinteger(source, 9007199254740993LL);
  lua_pushinteger(source, std::numeric_limits<std::int64_t>::min());
  lua_pushinteger(source, std::numeric_limits<std::int64_t>::max());
  lua_pushnumber(source, 1.25);
  lua_pushnumber(source, -0.0);
  lua_pushnumber(source, std::numeric_limits<double>::infinity());
  lua_pushnumber(source, std::numeric_limits<double>::quiet_NaN());
  const std::string binary("\0payload\xff\0", 10);
  lua_pushlstring(source, binary.data(), binary.size());
  lua_pushnil(source);
  const int count = lua_gettop(source);
  auto graph = captureLuaValues(source, -count, count);
  require(lua_gettop(source) == count && graph.roots.size() == 12,
          "Capture lost nil roots or changed the source stack");
  lua_pushliteral(destination, "sentinel");
  pushLuaValues(destination, graph);
  require(lua_gettop(destination) == count + 1 && lua_isnil(destination, 2) &&
              lua_isnil(destination, 13),
          "Push lost nil roots");
  require(lua_toboolean(destination, 3) && !lua_toboolean(destination, 4),
          "Boolean roots changed");
  for (int index = 4; index <= 6; ++index)
    require(lua_isinteger(destination, index + 1) &&
                lua_tointeger(destination, index + 1) ==
                    lua_tointeger(source, index),
            "Integer precision changed");
  require(lua_tonumber(destination, 8) == 1.25 &&
              std::signbit(lua_tonumber(destination, 9)) &&
              std::isinf(lua_tonumber(destination, 10)) &&
              std::isnan(lua_tonumber(destination, 11)),
          "Double roots changed");
  std::size_t length = 0;
  const char *text = lua_tolstring(destination, 12, &length);
  require(std::string(text, length) == binary, "Binary string changed");
  lua_settop(source, 0);
  lua_settop(destination, 0);
  auto empty = captureLuaValues(source, 0, 0);
  pushLuaValues(destination, empty);
  require(lua_gettop(destination) == 0, "Empty graph pushed a value");
}

void tableGraph(lua_State *source, lua_State *destination) {
  script(source, R"(
    local a, b = {}, {}
    a.self, a.other, b.other = a, b, a
    a[b], a[a], a[true], a[false], a[1.5] = b, a, 'yes', 'no', 'fraction'
    a[string.char(0, 255)] = string.char(255, 0)
    return a, nil, b, a
  )");
  auto graph = captureLuaValues(source, 1, 4);
  require(graph.tables.size() == 2, "Aliased tables duplicated in graph");
  lua_settop(source, 0);
  lua_gc(source, LUA_GCCOLLECT);
  pushLuaValues(destination, graph);
  require(lua_rawequal(destination, 1, 4), "Root alias lost");
  lua_setglobal(destination, "alias");
  lua_setglobal(destination, "b");
  lua_setglobal(destination, "nothing");
  lua_setglobal(destination, "a");
  script(destination, R"(
    assert(a == alias and nothing == nil)
    assert(a.self == a and a.other == b and b.other == a)
    assert(a[b] == b and a[a] == a)
    assert(a[true] == 'yes' and a[false] == 'no' and a[1.5] == 'fraction')
    assert(a[string.char(0, 255)] == string.char(255, 0))
  )");
  // Deep native graphs have no recursive ownership or conversion call stack.
  script(source, R"(
    local root = {}; local cursor = root
    for i = 1, 20000 do local child = {}; cursor.child = child; cursor = child end
    cursor.root = root; cursor.value = 'bottom'
    return root
  )");
  auto deep = captureLuaValues(source, 1, 1);
  require(deep.tables.size() == 20001, "Deep graph was truncated");
  pushLuaValues(destination, deep);
  lua_setglobal(destination, "deep");
  script(destination, R"(
    local cursor = deep
    for i = 1, 20000 do cursor = cursor.child end
    assert(cursor.value == 'bottom' and cursor.root == deep)
  )");
  lua_settop(source, 0);
}

void markers(lua_State *source, lua_State *destination) {
  for (const auto *kind : {"array", "object", "null"}) {
    lua_newtable(source);
    setLuaJsonTableKind(source, kind);
  }
  const auto graph = captureLuaValues(source, 1, 3);
  pushLuaValues(destination, graph);
  for (int index = 1; index <= 3; ++index) {
    const sol::object value(destination, index);
    const auto json = luaObjectToJson(value);
    require(index == 1   ? json.is_array()
            : index == 2 ? json.is_object()
                         : json.is_null(),
            "Data marker did not interoperate with JSON bridge");
  }
  lua_settop(source, 0);
  lua_settop(destination, 0);
}

void unsupported(lua_State *state) {
  for (const auto *source :
       {"return function() end", "return coroutine.create(function() end)",
        "return setmetatable({}, {})",
        "return setmetatable({}, {__demi_json_kind='unknown'})",
        "return setmetatable({}, {__demi_json_kind=42})",
        "return setmetatable({}, {__demi_json_kind='array', __index=function() "
        "error('called') end})",
        "return setmetatable({}, {__demi_json_kind='array', __metatable=true})",
        "return {nested={bad=function() end}}",
        "return {[function() end]=true}"}) {
    script(state, source);
    rejects(state, [&] { (void)captureLuaValues(state, -1, 1); });
    lua_settop(state, 0);
  }
  lua_newuserdatauv(state, 1, 0);
  rejects(state, [&] { (void)captureLuaValues(state, 1, 1); });
  lua_settop(state, 0);
  int native = 0;
  lua_pushlightuserdata(state, &native);
  rejects(state, [&] { (void)captureLuaValues(state, 1, 1); });
  rejects(state, [&] { (void)captureLuaValues(state, 1, -1); });
  rejects(state, [&] { (void)captureLuaValues(state, 0, 1); });
  rejects(state, [&] { (void)captureLuaValues(state, 1, 2); });
  rejects(state, [&] { (void)captureLuaValues(state, LUA_REGISTRYINDEX, 1); });
  lua_settop(state, 0);
}

void malformed(lua_State *state) {
  using Value = LuaTransferGraph::Value;
  LuaTransferGraph graph;
  Value table;
  table.kind = Value::Kind::Table;
  graph.roots.push_back(table);
  rejects(state, [&] { pushLuaValues(state, graph); });
  graph.tables.emplace_back();
  graph.tables[0].entries.emplace_back(Value{}, Value{});
  rejects(state, [&] { pushLuaValues(state, graph); });
  auto &key = graph.tables[0].entries[0].first;
  key.kind = Value::Kind::Number;
  key.number = std::numeric_limits<double>::quiet_NaN();
  rejects(state, [&] { pushLuaValues(state, graph); });
  key.kind = Value::Kind::Shared;
  rejects(state, [&] { pushLuaValues(state, graph); });
  graph.tables[0].entries.clear();
  graph.tables[0].marker = static_cast<LuaTransferGraph::Table::Marker>(999);
  rejects(state, [&] { pushLuaValues(state, graph); });
}

void shared(lua_State *source, lua_State *destination) {
  auto map = std::make_shared<LuaSharedMap>();
  pushLuaSharedMap(source, map);
  rejects(source, [&] { (void)captureLuaValues(source, 1, 1, false); });
  lua_newtable(source);
  lua_pushvalue(source, 1);
  lua_setfield(source, 2, "nested");
  rejects(source, [&] { (void)captureLuaValues(source, 2, 1, false); });
  lua_pushvalue(source, 1);
  lua_pushvalue(source, 1);
  lua_rawset(source, 2);
  rejects(source, [&] { (void)captureLuaValues(source, 2, 1, false); });
  auto graph = captureLuaValues(source, 1, 2);
  pushLuaValues(destination, graph);
  require(luaSharedMap(destination, 1) == map,
          "Shared native identity changed");
  lua_pushvalue(destination, 1);
  lua_rawget(destination, 2);
  require(lua_rawequal(destination, 1, -1), "SharedMap key alias changed");
  lua_pop(destination, 1);
  lua_getfield(destination, 2, "nested");
  require(lua_rawequal(destination, 1, -1), "SharedMap value alias changed");
  lua_settop(source, 0);
  lua_settop(destination, 0);
}
} // namespace

int main() {
  try {
    State source(luaL_newstate(), lua_close);
    State destination(luaL_newstate(), lua_close);
    require(source && destination, "Could not create Lua states");
    luaL_openlibs(source.get());
    luaL_openlibs(destination.get());
    scalarRoots(source.get(), destination.get());
    tableGraph(source.get(), destination.get());
    markers(source.get(), destination.get());
    unsupported(source.get());
    malformed(destination.get());
    shared(source.get(), destination.get());
  } catch (const std::exception &error) {
    std::cerr << "Lua transfer: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
