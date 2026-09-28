#include "demi/runtime/scripting/LuaWorkerContext.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <memory>
#include <stdexcept>
#include <utility>

namespace demi::runtime {
namespace {
char contextKey;
constexpr const char *contextMetatable = "demi.worker.context";

int destroyContext(lua_State *state) {
  std::destroy_at(static_cast<LuaWorkerContext *>(
      luaL_checkudata(state, 1, contextMetatable)));
  return 0;
}

void checkState(lua_State *state) {
  if (!state)
    throw std::invalid_argument("Lua worker context requires a Lua state");
}
} // namespace

LuaWorkerContext luaWorkerContext(lua_State *state) {
  checkState(state);
  lua_rawgetp(state, LUA_REGISTRYINDEX, &contextKey);
  const auto *context = static_cast<LuaWorkerContext *>(
      luaL_testudata(state, -1, contextMetatable));
  const auto result = context ? *context : LuaWorkerContext{};
  lua_pop(state, 1);
  return result;
}

void setLuaWorkerContext(lua_State *state, std::stop_token stopToken) {
  checkState(state);
  lua_rawgetp(state, LUA_REGISTRYINDEX, &contextKey);
  auto *context = static_cast<LuaWorkerContext *>(
      luaL_testudata(state, -1, contextMetatable));
  if (context) {
    *context = LuaWorkerContext{true, std::move(stopToken)};
    lua_pop(state, 1);
    return;
  }
  lua_pop(state, 1);
  if (luaL_newmetatable(state, contextMetatable)) {
    lua_pushcfunction(state, destroyContext);
    lua_setfield(state, -2, "__gc");
  }
  auto *storage = lua_newuserdatauv(state, sizeof(LuaWorkerContext), 0);
  std::construct_at(static_cast<LuaWorkerContext *>(storage),
                    LuaWorkerContext{true, std::move(stopToken)});
  lua_pushvalue(state, -2);
  lua_setmetatable(state, -2);
  lua_rawsetp(state, LUA_REGISTRYINDEX, &contextKey);
  lua_pop(state, 1);
}

void clearLuaWorkerContext(lua_State *state) {
  checkState(state);
  lua_rawgetp(state, LUA_REGISTRYINDEX, &contextKey);
  if (auto *context = static_cast<LuaWorkerContext *>(
          luaL_testudata(state, -1, contextMetatable))) {
    // Release the job's stop state immediately rather than waiting for Lua GC.
    *context = {};
  }
  lua_pop(state, 1);
  lua_pushnil(state);
  lua_rawsetp(state, LUA_REGISTRYINDEX, &contextKey);
}

AsyncWaitResult
waitLuaWorkerCompletion(lua_State *state,
                        std::shared_ptr<AsyncCompletion> completion,
                        std::optional<std::chrono::milliseconds> timeout) {
  const auto context = luaWorkerContext(state);
  if (!context.isWorker)
    throw std::runtime_error("Completion waits require a Lua worker context");
  if (!completion)
    throw std::invalid_argument("Completion wait requires a completion");
  return completion->wait(context.stopToken, timeout);
}

} // namespace demi::runtime
