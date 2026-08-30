#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scripting/LuaScriptHostInternal.h"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace demi::runtime {
namespace {

constexpr const char *RuntimeLogRegistryKey = "demi.runtime_log";

int runtimePrint(lua_State *state) {
  std::string message;
  const int count = lua_gettop(state);
  for (int index = 1; index <= count; ++index) {
    std::size_t length = 0;
    const char *text = luaL_tolstring(state, index, &length);
    if (index > 1)
      message.push_back('\t');
    if (text != nullptr)
      message.append(text, length);
    lua_pop(state, 1);
  }
  std::cout << message << '\n';
  std::string source;
  int line = 0;
  lua_Debug debug{};
  if (lua_getstack(state, 1, &debug) != 0 &&
      lua_getinfo(state, "Sl", &debug) != 0) {
    source = debug.source != nullptr ? debug.source : "";
    if (source.starts_with('@'))
      source.erase(source.begin());
    line = debug.currentline;
  }
  if (RuntimeLogBuffer *log = luaRuntimeLog(state))
    log->append({.severity = RuntimeLogSeverity::Info,
                 .channel = "lua",
                 .message = std::move(message),
                 .source = std::move(source),
                 .line = line,
                 .entityId = {},
                 .component = {},
                 .field = {}});
  return 0;
}

std::vector<std::string> consoleValues(lua_State *state, const int first) {
  std::vector<std::string> values;
  const int count = std::min(lua_gettop(state) - first + 1, 16);
  values.reserve(static_cast<std::size_t>(std::max(count, 0)));
  for (int offset = 0; offset < count; ++offset) {
    std::size_t length = 0;
    const char *text = luaL_tolstring(state, first + offset, &length);
    values.emplace_back(text != nullptr ? std::string(text, length) : "nil");
    lua_pop(state, 1);
  }
  return values;
}

} // namespace

void luaInstallRuntimeLogging(lua_State *state, RuntimeLogBuffer &log) {
  lua_pushlightuserdata(state, &log);
  lua_setfield(state, LUA_REGISTRYINDEX, RuntimeLogRegistryKey);
  lua_pushcfunction(state, runtimePrint);
  lua_setglobal(state, "print");
}

RuntimeLogBuffer *luaRuntimeLog(lua_State *state) {
  lua_getfield(state, LUA_REGISTRYINDEX, RuntimeLogRegistryKey);
  auto *log = static_cast<RuntimeLogBuffer *>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return log;
}

LuaScriptHost::ConsoleResult
LuaScriptHost::executeConsole(const std::string_view command) {
  ConsoleResult result;
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr) {
    result.error = "The Lua runtime is not initialized.";
    return result;
  }
  if (command.empty()) {
    result.error = "Enter a Lua expression or statement.";
    return result;
  }

  const int base = lua_gettop(state);
  const std::string expression = "return " + std::string(command);
  int status = luaL_loadbuffer(state, expression.data(), expression.size(),
                               "=editor-console");
  if (status != LUA_OK) {
    lua_settop(state, base);
    status = luaL_loadbuffer(state, command.data(), command.size(),
                             "=editor-console");
  }
  if (status == LUA_OK) {
    std::string error;
    if (luaCall(state, 0, LUA_MULTRET, error)) {
      result.values = consoleValues(state, base + 1);
      result.succeeded = true;
    } else {
      result.error = std::move(error);
    }
  } else {
    result.error = lua_tostring(state, -1) != nullptr
                       ? lua_tostring(state, -1)
                       : "Lua console compilation failed.";
  }
  lua_settop(state, base);

  std::string message;
  if (result.succeeded) {
    for (std::size_t index = 0; index < result.values.size(); ++index) {
      if (index > 0)
        message += "\t";
      message += result.values[index];
    }
    if (message.empty())
      message = "ok";
  } else {
    message = result.error;
  }
  runtimeLog_.append({.severity = result.succeeded ? RuntimeLogSeverity::Info
                                                   : RuntimeLogSeverity::Error,
                      .channel = "lua.console",
                      .message = std::move(message),
                      .source = "editor-console",
                      .line = 0,
                      .entityId = {},
                      .component = {},
                      .field = {}});
  return result;
}

} // namespace demi::runtime
