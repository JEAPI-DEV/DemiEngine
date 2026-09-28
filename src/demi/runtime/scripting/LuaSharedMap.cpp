#include "demi/runtime/scripting/LuaSharedMap.h"
#include "demi/runtime/scripting/lua_compat/lua.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace demi::runtime {
namespace {
constexpr const char *metatable = "demi.shared.map";
char contextKey;
struct Context {
  bool worker = false;
  const std::atomic<bool> *cancel = nullptr;
};
using Handle = std::shared_ptr<LuaSharedMap>;

Context &context(lua_State *L) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, &contextKey);
  auto *value = static_cast<Context *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return *value;
}

void acquire(lua_State *L, std::unique_lock<std::recursive_timed_mutex> &lock,
             bool tryOnly = false) {
  const auto &ctx = context(L);
  if (ctx.cancel && ctx.cancel->load())
    throw std::runtime_error("cancelled");
  if (!ctx.worker || tryOnly) {
    if (!lock.try_lock())
      throw std::runtime_error("busy");
    return;
  }
  while (!lock.try_lock_for(std::chrono::milliseconds(5))) {
    if (ctx.cancel && ctx.cancel->load())
      throw std::runtime_error("cancelled");
  }
  if (ctx.cancel && ctx.cancel->load()) {
    lock.unlock();
    throw std::runtime_error("cancelled");
  }
}

LuaSharedMap &mapAt(lua_State *L) {
  auto *handle = static_cast<Handle *>(luaL_testudata(L, 1, metatable));
  if (!handle || !*handle)
    throw std::invalid_argument("expected shared map");
  return **handle;
}

std::string keyAt(lua_State *L, int index) {
  if (lua_type(L, index) != LUA_TSTRING)
    throw std::invalid_argument("shared map keys must be strings");
  std::size_t length = 0;
  const char *value = lua_tolstring(L, index, &length);
  return {value, length};
}

template <int (*Operation)(lua_State *)> int guarded(lua_State *L) {
  try {
    return Operation(L);
  } catch (const std::exception &error) {
    lua_pushnil(L);
    lua_pushstring(L, error.what());
  }
  return 2;
}

int get(lua_State *L) {
  auto &map = mapAt(L);
  const auto key = keyAt(L, 2);
  std::shared_ptr<const LuaTransferGraph> graph;
  {
    std::unique_lock lock(map.mutex, std::defer_lock);
    acquire(L, lock);
    const auto found = map.values.find(key);
    if (found != map.values.end())
      graph = found->second;
  }
  if (!graph)
    lua_pushnil(L);
  else
    pushLuaValues(L, *graph);
  return 1;
}

int set(lua_State *L) {
  auto &map = mapAt(L);
  const auto key = keyAt(L, 2);
  if (lua_gettop(L) < 3)
    throw std::invalid_argument("set requires a value");
  auto graph = std::make_shared<const LuaTransferGraph>(
      captureLuaValues(L, 3, 1, false));
  {
    std::unique_lock lock(map.mutex, std::defer_lock);
    acquire(L, lock);
    map.values.insert_or_assign(key, std::move(graph));
  }
  lua_pushboolean(L, true);
  return 1;
}

int add(lua_State *L) {
  auto &map = mapAt(L);
  const auto key = keyAt(L, 2);
  if (lua_type(L, 3) != LUA_TNUMBER)
    throw std::invalid_argument("delta must be a number");
  const lua_Number delta = lua_tonumber(L, 3);
  if (!std::isfinite(delta))
    throw std::invalid_argument("delta must be finite");
  const bool integerDelta = lua_isinteger(L, 3);
  const auto increment = integerDelta ? lua_tointeger(L, 3) : 0;
  LuaTransferGraph::Value value;
  using Kind = LuaTransferGraph::Value::Kind;
  {
    std::unique_lock lock(map.mutex, std::defer_lock);
    acquire(L, lock);
    auto found = map.values.find(key);
    value.kind = Kind::Integer;
    if (found != map.values.end())
      value = found->second->roots.at(0);
    if (value.kind != Kind::Integer && value.kind != Kind::Number)
      throw std::invalid_argument("stored value must be a number");
    if (value.kind == Kind::Integer && integerDelta) {
      if ((increment > 0 &&
           value.integer >
               std::numeric_limits<std::int64_t>::max() - increment) ||
          (increment < 0 &&
           value.integer <
               std::numeric_limits<std::int64_t>::min() - increment))
        throw std::overflow_error("integer overflow");
      value.integer += increment;
    } else {
      const auto result =
          (value.kind == Kind::Integer ? static_cast<double>(value.integer)
                                       : value.number) +
          delta;
      if (!std::isfinite(result))
        throw std::overflow_error("number overflow");
      value.kind = Kind::Number;
      value.number = result;
    }
    LuaTransferGraph graph;
    graph.roots.push_back(value);
    map.values.insert_or_assign(
        key, std::make_shared<const LuaTransferGraph>(std::move(graph)));
  }
  if (value.kind == Kind::Integer)
    lua_pushinteger(L, value.integer);
  else
    lua_pushnumber(L, value.number);
  return 1;
}

int snapshot(lua_State *L) {
  auto &map = mapAt(L);
  decltype(map.values) values;
  {
    std::unique_lock lock(map.mutex, std::defer_lock);
    acquire(L, lock);
    values = map.values;
  }
  lua_newtable(L);
  for (const auto &[key, graph] : values) {
    lua_pushlstring(L, key.data(), key.size());
    pushLuaValues(L, *graph);
    lua_rawset(L, -3);
  }
  return 1;
}

template <bool TryOnly> int locked(lua_State *L) {
  // lua_pcall is deliberately non-yieldable. Never rethrow a Lua error until
  // the native lock's scope has ended (lua_error does not unwind C++ objects).
  if (!lua_isfunction(L, 2))
    return luaL_error(L, "locked callback must be a function");
  int status = LUA_OK;
  const int base = lua_gettop(L);
  try {
    auto &map = mapAt(L);
    std::unique_lock lock(map.mutex, std::defer_lock);
    acquire(L, lock, TryOnly);
    lua_pushboolean(L, true);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 1);
    status = lua_pcall(L, 1, LUA_MULTRET, 0);
  } catch (const std::exception &error) {
    lua_pushboolean(L, false);
    lua_pushstring(L, error.what());
    return 2;
  }
  if (status != LUA_OK)
    return lua_error(L);
  return lua_gettop(L) - base;
}

int collect(lua_State *L) {
  std::destroy_at(static_cast<Handle *>(lua_touserdata(L, 1)));
  return 0;
}

int equal(lua_State *L) {
  const auto left = luaSharedMap(L, 1);
  const auto right = luaSharedMap(L, 2);
  lua_pushboolean(L, left && left == right);
  return 1;
}

int create(lua_State *L) {
  auto map = std::make_shared<LuaSharedMap>();
  if (!lua_isnoneornil(L, 1)) {
    if (!lua_istable(L, 1))
      throw std::invalid_argument("initial value must be a table");
    lua_pushnil(L);
    while (lua_next(L, 1)) {
      auto key = keyAt(L, -2);
      map->values.emplace(std::move(key),
                          std::make_shared<const LuaTransferGraph>(
                              captureLuaValues(L, -1, 1, false)));
      lua_pop(L, 1);
    }
  }
  pushLuaSharedMap(L, std::move(map));
  return 1;
}

int open(lua_State *L) {
  lua_newtable(L);
  lua_pushcfunction(L, guarded<create>);
  lua_setfield(L, -2, "map");
  return 1;
}
} // namespace

void installLuaSharedBindings(lua_State *L, bool worker) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, &contextKey);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    auto *storage = lua_newuserdatauv(L, sizeof(Context), 0);
    std::construct_at(static_cast<Context *>(storage));
    lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &contextKey);
  }
  static_cast<Context *>(lua_touserdata(L, -1))->worker = worker;
  lua_pop(L, 1);
  if (luaL_newmetatable(L, metatable)) {
    const luaL_Reg methods[] = {{"get", guarded<get>},
                                {"set", guarded<set>},
                                {"add", guarded<add>},
                                {"snapshot", guarded<snapshot>},
                                {"with_lock", locked<false>},
                                {"try_lock", locked<true>},
                                {nullptr, nullptr}};
    lua_newtable(L);
    luaL_setfuncs(L, methods, 0);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, collect);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, equal);
    lua_setfield(L, -2, "__eq");
    lua_pushliteral(L, "protected shared map");
    lua_setfield(L, -2, "__metatable");
  }
  lua_pop(L, 1);
  luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
  lua_pushcfunction(L, open);
  lua_setfield(L, -2, "demi.shared");
  lua_pop(L, 1);
  if (!worker) {
    open(L);
    lua_setglobal(L, "Shared");
  }
}

void setLuaSharedCancellation(lua_State *L, const std::atomic<bool> *cancel) {
  context(L).cancel = cancel;
}

std::shared_ptr<LuaSharedMap> luaSharedMap(lua_State *L, int index) {
  auto *handle = static_cast<Handle *>(luaL_testudata(L, index, metatable));
  return handle ? *handle : nullptr;
}

void pushLuaSharedMap(lua_State *L, std::shared_ptr<LuaSharedMap> map) {
  if (!map) {
    lua_pushnil(L);
    return;
  }
  // Pushing transferred handles also works in VMs not explicitly installed yet.
  luaL_getmetatable(L, metatable);
  const bool missing = lua_isnil(L, -1);
  lua_pop(L, 1);
  if (missing)
    installLuaSharedBindings(L);
  auto *storage = lua_newuserdatauv(L, sizeof(Handle), 0);
  std::construct_at(static_cast<Handle *>(storage), std::move(map));
  luaL_setmetatable(L, metatable);
}
} // namespace demi::runtime
