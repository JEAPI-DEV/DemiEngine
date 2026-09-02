#include "demi/runtime/scripting/LuaScriptHostInternal.h"

#include "demi/runtime/scripting/bindings/LuaCoreBindings.h"
#include "demi/runtime/scripting/bindings/LuaEntityBindings.h"
#include "demi/runtime/scripting/bindings/LuaNetworkBindings.h"
#include "demi/runtime/scripting/bindings/LuaNetworkSessionBindings.h"
#include "demi/runtime/scripting/bindings/LuaRandomBindings.h"
#include "demi/runtime/scripting/bindings/LuaTlsBindings.h"
#include "demi/runtime/scripting/bindings/animation/LuaAnimationBindings.h"
#include "demi/runtime/scripting/bindings/assets/LuaAssetsBindings.h"
#include "demi/runtime/scripting/bindings/components/LuaCamera3DBindings.h"
#include "demi/runtime/scripting/bindings/components/LuaCharacterController3DBindings.h"
#include "demi/runtime/scripting/bindings/components/LuaPhysics2DBindings.h"
#include "demi/runtime/scripting/bindings/components/LuaPhysics3DBindings.h"
#include "demi/runtime/scripting/bindings/components/LuaRigidbody2DBindings.h"
#include "demi/runtime/scripting/bindings/components/LuaRigidbody3DBindings.h"
#include "demi/runtime/scripting/bindings/components/LuaSprite2DBindings.h"
#include "demi/runtime/scripting/bindings/components/LuaTilemap2DBindings.h"
#include "demi/runtime/scripting/bindings/components/LuaTransform2DBindings.h"
#include "demi/runtime/scripting/bindings/components/LuaTransform3DBindings.h"
#include "demi/runtime/scripting/bindings/data/LuaDataBindings.h"
#include "demi/runtime/scripting/bindings/hud/LuaHudBindings.h"
#include "demi/runtime/scripting/bindings/isometric/LuaIsoGridBindings.h"
#include "demi/runtime/scripting/bindings/media/LuaAudioBindings.h"
#include "demi/runtime/scripting/bindings/media/LuaCutsceneBindings.h"
#include "demi/runtime/scripting/bindings/media/LuaVideoBindings.h"
#include "demi/runtime/scripting/bindings/test/LuaTestBindings.h"
#include "demi/runtime/scripting/bindings/navigation/LuaNavigation2DBindings.h"
#include "demi/runtime/scripting/bindings/persistence/LuaSaveBindings.h"
#include "demi/runtime/scripting/bindings/text/LuaRegexBindings.h"
#include "demi/runtime/ui/UiModel.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace demi::runtime {

namespace {

int luaTraceback(lua_State *state) {
  const char *message = lua_tostring(state, 1);
  if (message != nullptr) {
    luaL_traceback(state, state, message, 1);
  } else {
    lua_pushliteral(state, "Lua error with non-string value");
  }
  return 1;
}

void installBindingModules(LuaScriptHost &host, lua_State *state) {
  const LuaCoreBindingModule core;
  const LuaEntityBindingModule entity;
  const LuaTransform2DBindingModule transform2D;
  const LuaTransform3DBindingModule transform3D;
  const LuaRigidbody2DBindingModule rigidbody2D;
  const LuaRigidbody3DBindingModule rigidbody3D;
  const LuaCharacterController3DBindingModule characterController3D;
  const LuaCamera3DBindingModule camera3D;
  const LuaSprite2DBindingModule sprite2D;
  const LuaPhysics2DBindingModule physics2D;
  const LuaPhysics3DBindingModule physics3D;
  const LuaHudBindingModule hud;
  const LuaSaveBindingModule save;
  const LuaAudioBindingModule audio;
  const LuaVideoBindingModule video;
  const LuaCutsceneBindingModule cutscene;
  const LuaNetworkBindingModule network;
  const LuaNetworkSessionBindingModule networkSession;
  const LuaTlsBindingModule tls;
  const LuaRegexBindingModule regex;
  const LuaRandomBindingModule random;
  const LuaIsoGridBindingModule isoGrid;
  const LuaAnimationBindingModule animation;
  const LuaAssetsBindingModule assets;
  const LuaNavigation2DBindingModule navigation2D;
  const LuaTilemap2DBindingModule tilemap2D;
  const LuaDataBindingModule data;
  const LuaTestBindingModule e2eTests;
  const LuaBindingModule *modules[] = {&core,
                                       &entity,
                                       &transform2D,
                                       &transform3D,
                                       &rigidbody2D,
                                       &rigidbody3D,
                                       &characterController3D,
                                       &camera3D,
                                       &sprite2D,
                                       &physics2D,
                                       &physics3D,
                                       &hud,
                                       &save,
                                       &audio,
                                       &video,
                                       &cutscene,
                                       &network,
                                       &networkSession,
                                       &tls,
                                       &regex,
                                       &random,
                                       &isoGrid,
                                       &animation,
                                       &assets,
                                       &navigation2D,
                                       &tilemap2D,
                                       &data,
                                       &e2eTests};
  for (const LuaBindingModule *module : modules) {
    module->install(host, state);
  }
}

void registerSol2Bindings(LuaScriptHost &host, lua_State *state) {
  installBindingModules(host, state);
}

} // namespace

bool luaCall(lua_State *state, const int argCount, const int resultCount,
             std::string &error) {
  const int functionIndex = lua_gettop(state) - argCount;
  lua_pushcfunction(state, luaTraceback);
  lua_insert(state, functionIndex);
  const int status = lua_pcall(state, argCount, resultCount, functionIndex);
  lua_remove(state, functionIndex);
  if (status != LUA_OK) {
    error = lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1)
                                               : "Lua callback failed.";
    lua_pop(state, 1);
    return false;
  }
  return true;
}

std::vector<std::string> LuaScriptHost::publicLuaApi() const {
  auto *state = static_cast<lua_State *>(state_);
  std::vector<std::string> result;
  if (state == nullptr) {
    return result;
  }

  lua_pushglobaltable(state);
  const int globalsIndex = lua_gettop(state);
  lua_pushnil(state);
  while (lua_next(state, globalsIndex) != 0) {
    const char *serviceName =
        lua_type(state, -2) == LUA_TSTRING ? lua_tostring(state, -2) : nullptr;
    const bool publicService =
        serviceName != nullptr && serviceName[0] != '\0' &&
        std::isupper(static_cast<unsigned char>(serviceName[0])) != 0 &&
        lua_istable(state, -1);
    if (publicService) {
      const int serviceIndex = lua_gettop(state);
      lua_pushnil(state);
      while (lua_next(state, serviceIndex) != 0) {
        const char *functionName = lua_type(state, -2) == LUA_TSTRING
                                       ? lua_tostring(state, -2)
                                       : nullptr;
        if (functionName != nullptr && lua_isfunction(state, -1)) {
          result.emplace_back(std::string(serviceName) + "." + functionName);
        }
        lua_pop(state, 1);
      }
    }
    lua_pop(state, 1);
  }
  lua_pop(state, 1);

  std::ranges::sort(result);
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

void luaReportCallbackError(lua_State *state, const char *functionName,
                            const std::filesystem::path &path,
                            const std::string &ownerId,
                            const std::string &error) {
  std::cerr << "Lua " << functionName << " failed";
  if (!path.empty()) {
    std::cerr << " in " << path.string();
  }
  if (!ownerId.empty()) {
    std::cerr << " for " << ownerId;
  }
  std::cerr << ": " << error << '\n';
  if (RuntimeLogBuffer *log = luaRuntimeLog(state))
    log->append({.severity = RuntimeLogSeverity::Error,
                 .channel = "lua.callback",
                 .message = std::string(functionName) + ": " + error,
                 .source = path.string(),
                 .line = 0,
                 .entityId = ownerId,
                 .component = "LuaScript",
                 .field = {}});
}

std::filesystem::path luaResolveScriptPath(const ProjectData &project,
                                           const std::string &module) {
  constexpr std::string_view prefix = "script://";
  return module.starts_with(prefix)
             ? project.projectDirectory / module.substr(prefix.size())
             : project.projectDirectory / module;
}

void luaConfigurePackagePath(lua_State *state, const ProjectData &project) {
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, "path");
  const char *currentPath = lua_tostring(state, -1);
  const std::string current = currentPath != nullptr ? currentPath : "";
  lua_pop(state, 1);
  const std::filesystem::path scripts = project.projectDirectory / "scripts";
  const std::filesystem::path runtimeScripts =
      std::filesystem::path(DEMI_SOURCE_DIR) / "scripts" / "runtime";
  std::string packagePaths;
  std::vector<std::filesystem::path> roots;
  for (const std::filesystem::path &packages :
       {project.projectDirectory / ".demi" / "packages",
        project.projectDirectory / "packages"}) {
    std::error_code packageError;
    if (!std::filesystem::is_directory(packages, packageError))
      continue;
    for (const auto &entry : std::filesystem::directory_iterator(packages))
      if (entry.is_directory())
        roots.push_back(entry.path() / "scripts");
  }
  std::ranges::sort(roots);
  roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
  for (const auto &root : roots)
    packagePaths += root.string() + "/?.lua;" + root.string() + "/?/init.lua;";
  const std::string path = packagePaths + scripts.string() + "/?.lua;" +
                           scripts.string() + "/?/init.lua;" +
                           project.projectDirectory.string() + "/?.lua;" +
                           project.projectDirectory.string() + "/?/init.lua;" +
                           runtimeScripts.string() + "/?.lua;" +
                           runtimeScripts.string() + "/?/init.lua;" + current;
  lua_pushstring(state, path.c_str());
  lua_setfield(state, -2, "path");
  lua_pop(state, 1);
}

std::filesystem::file_time_type
luaScriptWriteTime(const std::filesystem::path &path) {
  std::error_code error;
  const std::filesystem::file_time_type time =
      std::filesystem::last_write_time(path, error);
  return error ? std::filesystem::file_time_type{} : time;
}

bool luaLoadScriptTable(lua_State *state,
                        const std::filesystem::path &scriptPath,
                        std::string &error) {
  if (luaL_loadfile(state, scriptPath.string().c_str()) != LUA_OK) {
    error = lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1)
                                               : "Lua syntax error.";
    lua_pop(state, 1);
    return false;
  }
  if (!luaCall(state, 0, 1, error)) {
    return false;
  }
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    error = "script must return a table";
    return false;
  }
  return true;
}

void luaCallLifecycle(lua_State *state, const int tableRef,
                      const char *functionName,
                      const std::filesystem::path &path,
                      const std::string &ownerId, const float dt) {
  lua_rawgeti(state, LUA_REGISTRYINDEX, tableRef);
  lua_getfield(state, -1, functionName);
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }
  lua_pushvalue(state, -2);
  int argCount = 1;
  if (functionName == std::string_view("on_update") ||
      functionName == std::string_view("on_fixed_update")) {
    lua_pushnumber(state, dt);
    argCount = 2;
  }
  std::string error;
  if (!luaCall(state, argCount, 0, error)) {
    luaReportCallbackError(state, functionName, path, ownerId, error);
  }
  lua_pop(state, 1);
}

void luaPushUiEvent(lua_State *state, const ui::UiEvent &event) {
  lua_newtable(state);
  const std::string_view type = ui::uiEventTypeName(event.type);
  lua_pushlstring(state, type.data(), type.size());
  lua_setfield(state, -2, "type");
  lua_pushstring(state, event.id.c_str());
  lua_setfield(state, -2, "id");
  lua_pushstring(state, event.relatedId.c_str());
  lua_setfield(state, -2, "related_id");
  lua_pushstring(state, event.action.c_str());
  lua_setfield(state, -2, "action");
  lua_pushstring(state, event.text.c_str());
  lua_setfield(state, -2, "text");
  lua_pushstring(state, event.source.c_str());
  lua_setfield(state, -2, "source");
  lua_pushinteger(state, static_cast<lua_Integer>(event.pointerId));
  lua_setfield(state, -2, "pointer_id");
  lua_pushnumber(state, event.position.x);
  lua_setfield(state, -2, "x");
  lua_pushnumber(state, event.position.y);
  lua_setfield(state, -2, "y");
  lua_pushnumber(state, event.delta.x);
  lua_setfield(state, -2, "delta_x");
  lua_pushnumber(state, event.delta.y);
  lua_setfield(state, -2, "delta_y");
  lua_pushnumber(state, event.value);
  lua_setfield(state, -2, "value");
  lua_pushboolean(state, event.checked);
  lua_setfield(state, -2, "checked");
  lua_pushboolean(state, event.cancelled);
  lua_setfield(state, -2, "cancelled");
}

void luaCallTypedUiEvent(lua_State *state, const int tableRef,
                         const char *functionName, const ui::UiEvent &event,
                         const std::filesystem::path &path) {
  lua_rawgeti(state, LUA_REGISTRYINDEX, tableRef);
  lua_getfield(state, -1, functionName);
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }
  lua_pushvalue(state, -2);
  luaPushUiEvent(state, event);
  std::string error;
  if (!luaCall(state, 2, 0, error))
    luaReportCallbackError(state, functionName, path, event.id, error);
  lua_pop(state, 1);
}

void luaCallActionEvent(lua_State *state, const int tableRef,
                        const std::string &functionName, const ui::UiNode &node,
                        const Vec2 mousePosition,
                        const std::filesystem::path &path) {
  lua_rawgeti(state, LUA_REGISTRYINDEX, tableRef);
  lua_getfield(state, -1, functionName.c_str());
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }
  lua_pushvalue(state, -2);
  lua_newtable(state);
  lua_pushstring(state, node.id.c_str());
  lua_setfield(state, -2, "id");
  lua_pushstring(state, node.text.c_str());
  lua_setfield(state, -2, "label");
  lua_pushstring(state, node.action.c_str());
  lua_setfield(state, -2, "action");
  lua_pushnumber(state, mousePosition.x);
  lua_setfield(state, -2, "mouse_x");
  lua_pushnumber(state, mousePosition.y);
  lua_setfield(state, -2, "mouse_y");
  std::string error;
  if (!luaCall(state, 2, 0, error)) {
    luaReportCallbackError(state, functionName.c_str(), path, node.action,
                           error);
  }
  lua_pop(state, 1);
}

void luaCallModuleActionEvent(lua_State *state, const std::string &moduleName,
                              const std::string &functionName,
                              const ui::UiNode &node, const Vec2 mousePosition,
                              const std::filesystem::path &path) {
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, "loaded");
  lua_remove(state, -2);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, moduleName.c_str());
  lua_remove(state, -2);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, functionName.c_str());
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }
  lua_newtable(state);
  lua_pushstring(state, node.id.c_str());
  lua_setfield(state, -2, "id");
  lua_pushstring(state, node.text.c_str());
  lua_setfield(state, -2, "label");
  lua_pushstring(state, node.action.c_str());
  lua_setfield(state, -2, "action");
  lua_pushnumber(state, mousePosition.x);
  lua_setfield(state, -2, "mouse_x");
  lua_pushnumber(state, mousePosition.y);
  lua_setfield(state, -2, "mouse_y");
  std::string error;
  if (!luaCall(state, 1, 0, error)) {
    luaReportCallbackError(state, functionName.c_str(), path, node.action,
                           error);
  }
  lua_pop(state, 1);
}

void luaCallScriptEvent(lua_State *state, const int tableRef,
                        const std::string &functionName, const int payloadIndex,
                        const std::filesystem::path &path,
                        const std::string &eventName) {
  lua_rawgeti(state, LUA_REGISTRYINDEX, tableRef);
  lua_getfield(state, -1, functionName.c_str());
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }
  lua_pushvalue(state, -2);
  payloadIndex > 0 ? lua_pushvalue(state, payloadIndex) : lua_newtable(state);
  std::string error;
  if (!luaCall(state, 2, 0, error)) {
    luaReportCallbackError(state, functionName.c_str(), path, eventName, error);
  }
  lua_pop(state, 1);
}

void luaCallModuleEvent(lua_State *state, const std::string &moduleName,
                        const std::string &functionName, const int payloadIndex,
                        const std::filesystem::path &path,
                        const std::string &eventName) {
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, "loaded");
  lua_remove(state, -2);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, moduleName.c_str());
  lua_remove(state, -2);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, functionName.c_str());
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }
  payloadIndex > 0 ? lua_pushvalue(state, payloadIndex) : lua_newtable(state);
  std::string error;
  if (!luaCall(state, 1, 0, error)) {
    luaReportCallbackError(state, functionName.c_str(), path, eventName, error);
  }
  lua_pop(state, 1);
}

bool luaRegisterBindings(LuaScriptHost &host, lua_State *state,
                         std::string &error) {
  if (state == nullptr) {
    error = "Lua state was not initialized.";
    return false;
  }
  registerSol2Bindings(host, state);
  return true;
}

} // namespace demi::runtime
