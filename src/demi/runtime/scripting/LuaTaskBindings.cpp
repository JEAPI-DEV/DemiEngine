#include "demi/runtime/scripting/LuaWorkerTasks.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace demi::runtime {
namespace {
constexpr const char *HandleType = "demi.task.handle";
constexpr const char *ContextType = "demi.task.context";
using Handle = std::shared_ptr<LuaWorkerTask>;

struct BindingContext {
  LuaWorkerTasks *owner;
  std::weak_ptr<void> lifetime;
};

LuaWorkerTasks *owner(lua_State *state) {
  const auto *context =
      static_cast<BindingContext *>(lua_touserdata(state, lua_upvalueindex(1)));
  if (!context || context->lifetime.expired())
    luaL_error(state, "Task worker service is unavailable");
  return context->owner;
}

Handle &handle(lua_State *state) {
  return *static_cast<Handle *>(luaL_checkudata(state, 1, HandleType));
}

template <class Function>
int protectedCall(lua_State *state, Function function) {
  try {
    return function();
  } catch (const std::exception &error) {
    lua_pushstring(state, error.what());
  }
  return lua_error(state);
}

int releaseHandle(lua_State *state) {
  std::destroy_at(static_cast<Handle *>(luaL_checkudata(state, 1, HandleType)));
  return 0;
}

int releaseContext(lua_State *state) {
  std::destroy_at(
      static_cast<BindingContext *>(luaL_checkudata(state, 1, ContextType)));
  return 0;
}

int status(lua_State *state) {
  lua_pushstring(state, luaWorkerTaskStatusName(handle(state)->status()));
  return 1;
}

int done(lua_State *state) {
  lua_pushboolean(state, handle(state)->done());
  return 1;
}

int cancel(lua_State *state) {
  return protectedCall(state, [&] {
    lua_pushboolean(state, handle(state)->cancel());
    return 1;
  });
}

int error(lua_State *state) {
  return protectedCall(state, [&] {
    const auto message = handle(state)->error();
    if (message.empty())
      lua_pushnil(state);
    else
      lua_pushlstring(state, message.data(), message.size());
    return 1;
  });
}

int result(lua_State *state) {
  return protectedCall(state, [&] { return handle(state)->pushResult(state); });
}

int cancelled(lua_State *state) {
  lua_pushboolean(state, false);
  return 1;
}

int checkCancelled(lua_State *) { return 0; }

int fork(lua_State *state) {
  auto *tasks = owner(state);
  luaL_checktype(state, 1, LUA_TFUNCTION);
  return protectedCall(state, [&] {
    auto task = tasks->fork(state, 1, 2, lua_gettop(state) - 1);
    std::construct_at(
        static_cast<Handle *>(lua_newuserdatauv(state, sizeof(Handle), 0)),
        std::move(task));
    luaL_setmetatable(state, HandleType);
    return 1;
  });
}

int configure(lua_State *state) {
  auto *tasks = owner(state);
  luaL_checktype(state, 1, LUA_TTABLE);
  return protectedCall(state, [&] {
    lua_getfield(state, 1, "workers");
    int valid = 0;
    const lua_Integer count = lua_tointegerx(state, -1, &valid);
    const bool number = lua_type(state, -1) == LUA_TNUMBER;
    lua_pop(state, 1);
    if (!number || !valid || count < 1 ||
        static_cast<lua_Unsigned>(count) >
            std::numeric_limits<std::size_t>::max())
      throw std::invalid_argument(
          "Task.configure workers must be a positive integer");
    lua_pushnil(state);
    while (lua_next(state, 1)) {
      std::size_t length = 0;
      const char *key = lua_type(state, -2) == LUA_TSTRING
                            ? lua_tolstring(state, -2, &length)
                            : nullptr;
      const bool known = key && std::string_view(key, length) == "workers";
      lua_pop(state, 1);
      if (!known) {
        lua_pop(state, 1);
        throw std::invalid_argument("Task.configure supports only workers");
      }
    }
    tasks->configure(static_cast<std::size_t>(count));
    return 0;
  });
}
} // namespace

void installLuaTaskBindings(lua_State *state, LuaWorkerTasks &tasks) {
  luaL_newmetatable(state, HandleType);
  lua_pushcfunction(state, releaseHandle);
  lua_setfield(state, -2, "__gc");
  lua_pushliteral(state, "protected task handle");
  lua_setfield(state, -2, "__metatable");
  lua_newtable(state);
  const luaL_Reg methods[] = {{"status", status}, {"done", done},
                              {"cancel", cancel}, {"result", result},
                              {"error", error},   {nullptr, nullptr}};
  luaL_setfuncs(state, methods, 0);
  lua_setfield(state, -2, "__index");
  lua_pop(state, 1);

  luaL_newmetatable(state, ContextType);
  lua_pushcfunction(state, releaseContext);
  lua_setfield(state, -2, "__gc");
  lua_pop(state, 1);
  std::construct_at(static_cast<BindingContext *>(
                        lua_newuserdatauv(state, sizeof(BindingContext), 0)),
                    BindingContext{&tasks, tasks.bindingLifetime_});
  luaL_setmetatable(state, ContextType);
  lua_newtable(state);
  lua_pushvalue(state, -2);
  const luaL_Reg functions[] = {{"fork", fork},
                                {"configure", configure},
                                {"cancelled", cancelled},
                                {"check_cancelled", checkCancelled},
                                {nullptr, nullptr}};
  luaL_setfuncs(state, functions, 1);
  lua_setglobal(state, "Task");
  lua_pop(state, 1);
}
} // namespace demi::runtime
