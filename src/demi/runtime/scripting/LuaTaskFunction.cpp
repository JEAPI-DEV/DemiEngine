#include "demi/runtime/scripting/LuaTaskFunction.h"

#include "demi/runtime/scripting/LuaSharedMap.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <exception>
#include <stdexcept>
#include <utility>

namespace demi::runtime {
namespace {
struct StackScope {
  lua_State *state;
  int top;
  ~StackScope() { lua_settop(state, top); }
};

struct DumpBuffer {
  std::string bytes;
  std::exception_ptr error;
};

int writeBytecode(lua_State *, const void *bytes, std::size_t size,
                  void *context) {
  auto &buffer = *static_cast<DumpBuffer *>(context);
  try {
    buffer.bytes.append(static_cast<const char *>(bytes), size);
    return 0;
  } catch (...) {
    buffer.error = std::current_exception();
    return 1;
  }
}

int invoke(lua_State *state) {
  const auto &function = *static_cast<const LuaTaskFunction *>(
      lua_touserdata(state, lua_upvalueindex(1)));
  try {
    if (luaL_loadbufferx(state, function.bytecode.data(),
                         function.bytecode.size(), "Lua task callback",
                         "b") != LUA_OK)
      return lua_error(state);
    for (const auto &upvalue : function.upvalues) {
      if (upvalue.environment)
        lua_pushvalue(state, lua_upvalueindex(2));
      else
        pushLuaValues(state, upvalue.value);
      if (!lua_setupvalue(state, 1, upvalue.index))
        return luaL_error(state, "Could not restore a task function upvalue");
    }
    pushLuaValues(state, function.arguments);
    lua_call(state, static_cast<int>(function.arguments.roots.size()),
             LUA_MULTRET);
    return lua_gettop(state);
  } catch (const std::exception &error) {
    lua_pushstring(state, error.what());
  }
  return lua_error(state);
}
} // namespace

LuaTaskFunction captureLuaTaskFunction(lua_State *state, int functionIndex,
                                       int firstArg, int argCount,
                                       std::string_view operation) {
  const StackScope stack{state, lua_gettop(state)};
  functionIndex = lua_absindex(state, functionIndex);
  firstArg = lua_absindex(state, firstArg);
  const std::string prefix(operation);
  if (functionIndex < 1 || functionIndex > stack.top ||
      !lua_isfunction(state, functionIndex) ||
      lua_iscfunction(state, functionIndex))
    throw std::invalid_argument(
        prefix + " requires a Lua function, not a native function");
  if (argCount < 0 || (argCount > 0 && (firstArg < 1 || firstArg > stack.top ||
                                        argCount > stack.top - firstArg + 1)))
    throw std::invalid_argument(prefix + " argument range is invalid");

  LuaTaskFunction function;
  function.arguments = captureLuaValues(state, firstArg, argCount);
  lua_pushvalue(state, functionIndex);
  const int copiedFunction = lua_gettop(state);
  DumpBuffer dump;
  const int status = lua_dump(state, writeBytecode, &dump, 0);
  if (dump.error)
    std::rethrow_exception(dump.error);
  if (status != 0)
    throw std::invalid_argument(prefix +
                                " could not serialize the Lua function");
  function.bytecode = std::move(dump.bytes);
  for (int index = 1;; ++index) {
    const char *name = lua_getupvalue(state, copiedFunction, index);
    if (!name)
      break;
    if (std::string_view(name) == "_ENV") {
      lua_pushglobaltable(state);
      const bool normalEnvironment = lua_rawequal(state, -1, -2);
      lua_pop(state, 2);
      if (!normalEnvironment)
        throw std::invalid_argument(prefix + " rejects a custom _ENV");
      function.upvalues.push_back({index, true, {}});
      continue;
    }
    const int type = lua_type(state, -1);
    const bool primitive = type == LUA_TNIL || type == LUA_TBOOLEAN ||
                           type == LUA_TNUMBER || type == LUA_TSTRING;
    if (!primitive && !luaSharedMap(state, -1))
      throw std::invalid_argument(
          prefix + " cannot capture upvalue '" + name + "' (" +
          lua_typename(state, type) +
          "); capture primitive values or SharedMap handles, and pass table "
          "snapshots as arguments");
    function.upvalues.push_back({index, false, captureLuaValues(state, -1, 1)});
    lua_pop(state, 1);
  }
  return function;
}

int invokeLuaTaskFunction(lua_State *state, const LuaTaskFunction &function,
                          int environmentIndex) {
  const int top = lua_gettop(state);
  if (environmentIndex != 0) {
    environmentIndex = lua_absindex(state, environmentIndex);
    if (!lua_istable(state, environmentIndex))
      throw std::invalid_argument("Task environment must be a table");
  }
  lua_pushlightuserdata(state, const_cast<LuaTaskFunction *>(&function));
  if (environmentIndex == 0)
    lua_pushglobaltable(state);
  else
    lua_pushvalue(state, environmentIndex);
  lua_pushcclosure(state, invoke, 2);
  if (lua_pcall(state, 0, LUA_MULTRET, 0) != LUA_OK) {
    const StackScope stack{state, top};
    const char *message = lua_tostring(state, -1);
    throw std::runtime_error(message ? message
                                     : "Lua task raised a non-string error");
  }
  return lua_gettop(state) - top;
}
} // namespace demi::runtime
