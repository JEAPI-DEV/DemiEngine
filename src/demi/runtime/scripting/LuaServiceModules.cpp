#include "demi/runtime/scripting/LuaServiceModules.h"
extern "C" {
#include <lua.h>
}
#include <cctype>
#include <vector>

namespace demi::runtime {
std::string luaServiceModuleName(std::string_view service) {
  if (service == "Transform")
    return "demi.transform2d";
  std::string result = "demi.";
  for (std::size_t i = 0; i < service.size(); ++i) {
    const auto c = static_cast<unsigned char>(service[i]);
    if (i && std::isupper(c) &&
        std::islower(static_cast<unsigned char>(service[i - 1])))
      result += '_';
    result += static_cast<char>(std::tolower(c));
  }
  return result;
}
void publishLuaServiceModules(lua_State *state) {
  lua_newtable(state);
  const int services = lua_gettop(state);
  lua_getglobal(state, "package");
  lua_getfield(state, -1, "preload");
  lua_remove(state, -2);
  const int preload = lua_gettop(state);
  lua_pushglobaltable(state);
  const int globals = lua_gettop(state);
  std::vector<std::string> names;
  lua_pushnil(state);
  while (lua_next(state, globals)) {
    if (lua_type(state, -2) == LUA_TSTRING && lua_istable(state, -1)) {
      const std::string name = lua_tostring(state, -2);
      if (!name.empty() &&
          std::isupper(static_cast<unsigned char>(name.front()))) {
        names.push_back(name);
        lua_pushvalue(state, -1);
        lua_setfield(state, services, name.c_str());
        lua_pushvalue(state, -1);
        lua_pushcclosure(
            state,
            [](lua_State *s) {
              lua_pushvalue(s, lua_upvalueindex(1));
              return 1;
            },
            1);
        lua_setfield(state, preload, luaServiceModuleName(name).c_str());
      }
    }
    lua_pop(state, 1);
  }
  for (const auto &name : names) {
    lua_pushnil(state);
    lua_setfield(state, globals, name.c_str());
  }
  lua_pop(state, 2);
  lua_setfield(state, LUA_REGISTRYINDEX, LuaServicesRegistry);
}
void pushLuaService(lua_State *state, const char *service) {
  lua_getfield(state, LUA_REGISTRYINDEX, LuaServicesRegistry);
  if (lua_istable(state, -1)) {
    lua_getfield(state, -1, service);
    lua_remove(state, -2);
  } else {
    lua_pop(state, 1);
    lua_pushnil(state);
  }
}
void clearLuaServiceModules(lua_State *state) {
  lua_getfield(state, LUA_REGISTRYINDEX, LuaServicesRegistry);
  if (lua_istable(state, -1)) {
    lua_pushnil(state);
    while (lua_next(state, -2)) {
      const auto module = luaServiceModuleName(lua_tostring(state, -2));
      lua_getglobal(state, "package");
      for (const char *field : {"loaded", "preload"}) {
        lua_getfield(state, -1, field);
        lua_pushnil(state);
        lua_setfield(state, -2, module.c_str());
        lua_pop(state, 1);
      }
      lua_pop(state, 2);
    }
  }
  lua_pop(state, 1);
  lua_pushnil(state);
  lua_setfield(state, LUA_REGISTRYINDEX, LuaServicesRegistry);
}
} // namespace demi::runtime
