#include "demi/runtime/scripting/LuaServiceModules.h"
extern "C" {
#include <lua.h>
}
#include <stdexcept>

namespace demi::runtime {
namespace {
constexpr LuaServiceModule serviceModules[] = {
    {"Animation", "demi.animation"},
    {"Application", "demi.application"},
    {"Assets", "demi.assets"},
    {"Audio", "demi.audio"},
    {"AudioSource", "demi.audio.source"},
    {"Camera3D", "demi.camera3d"},
    {"CharacterController3D", "demi.physics.character_controller3d"},
    {"Crypto", "demi.network.crypto"},
    {"Cutscene", "demi.cutscene"},
    {"Data", "demi.data"},
    {"Debug", "demi.debug"},
    {"Destruction3D", "demi.physics.destruction3d"},
    {"Entity", "demi.entity"},
    {"Events", "demi.events"},
    {"Grid", "demi.grid"},
    {"Hud", "demi.hud"},
    {"Input", "demi.input"},
    {"Mathf", "demi.math.scalar"},
    {"MeshDeformation", "demi.mesh.deformation"},
    {"Navigation2D", "demi.navigation2d"},
    {"Network", "demi.network"},
    {"NetworkSession", "demi.network.session"},
    {"Physics", "demi.physics"},
    {"Physics2D", "demi.physics.query2d"},
    {"Physics3D", "demi.physics.query3d"},
    {"Prefab", "demi.prefab"},
    {"ProceduralMesh", "demi.mesh.procedural"},
    {"Profile", "demi.profile"},
    {"Random", "demi.math.random"},
    {"Regex", "demi.regex"},
    {"Rigidbody2D", "demi.physics.rigidbody2d"},
    {"Rigidbody3D", "demi.physics.rigidbody3d"},
    {"Save", "demi.save"},
    {"Scene", "demi.scene"},
    {"Sprite2D", "demi.sprite2d"},
    {"Test", "demi.test"},
    {"Text", "demi.text"},
    {"Tilemap2D", "demi.tilemap2d"},
    {"Time", "demi.time"},
    {"Timer", "demi.timer"},
    {"TlsClient", "demi.network.tls.client"},
    {"TlsServer", "demi.network.tls.server"},
    {"Transform", "demi.transform2d"},
    {"Transform3D", "demi.transform3d"},
    {"Vector2", "demi.math.vector2"},
    {"Vector3", "demi.math.vector3"},
    {"Video", "demi.video"},
    {"VoxelWorld", "demi.voxel.world"},
};
} // namespace

std::span<const LuaServiceModule> luaServiceModules() {
  return serviceModules;
}

std::string luaServiceModuleName(std::string_view service) {
  for (const auto &entry : serviceModules) {
    if (service == entry.service) {
      return std::string(entry.module);
    }
  }
  throw std::invalid_argument("Unknown native Lua service: " + std::string(service));
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
  for (const auto &entry : serviceModules) {
    lua_getfield(state, globals, entry.service.data());
    if (!lua_istable(state, -1)) {
      lua_pop(state, 1);
      continue;
    }
    lua_pushvalue(state, -1);
    lua_setfield(state, services, entry.service.data());
    lua_pushcclosure(
        state,
        [](lua_State *moduleState) {
          lua_pushvalue(moduleState, lua_upvalueindex(1));
          return 1;
        },
        1);
    lua_setfield(state, preload, entry.module.data());
    lua_pushnil(state);
    lua_setfield(state, globals, entry.service.data());
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
