#include "demi/runtime/LuaScriptHost.h"

#include "demi/runtime/LuaScriptHostInternal.h"

#include <iostream>
#include <utility>

namespace demi::runtime {

LuaScriptHost::LuaScriptHost() = default;

LuaScriptHost::~LuaScriptHost() {
  destroy();
#if DEMI_HAS_LUA54
  if (state_ != nullptr) {
    lua_close(static_cast<lua_State*>(state_));
    state_ = nullptr;
  }
#endif
}

bool LuaScriptHost::initialize(World& world, const InputState& input, AudioSystem* audio, std::string& error) {
#if !DEMI_HAS_LUA54
  (void)world;
  (void)input;
  (void)audio;
  error = "Lua 5.4 support is unavailable because lua5.4 was not found at configure time.";
  return false;
#else
  world_ = &world;
  input_ = &input;
  audio_ = audio;

  auto* state = luaL_newstate();
  if (state == nullptr) {
    error = "Failed to allocate Lua state.";
    return false;
  }
  luaL_openlibs(state);
  state_ = state;

  return luaRegisterBindings(*this, state, error);
#endif
}

bool LuaScriptHost::loadWorldScripts(const ProjectData& project, World& world, std::string& error) {
#if !DEMI_HAS_LUA54
  (void)project;
  (void)world;
  error = "Lua 5.4 support is unavailable.";
  return false;
#else
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr) {
    error = "LuaScriptHost was not initialized.";
    return false;
  }

  projectDirectory_ = project.projectDirectory;
  project_ = &project;
  luaConfigurePackagePath(state, project);

  auto loadScript = [&](std::string entityId, const std::string& module, const char* context) -> bool {
    const std::filesystem::path scriptPath = luaResolveScriptPath(project, module);
    std::string scriptError;
    if (!luaLoadScriptTable(state, scriptPath, scriptError)) {
      error = std::string("Failed to load ") + context + " " + scriptPath.string() + ": " + scriptError;
      return false;
    }

    if (!entityId.empty()) {
      lua_pushstring(state, entityId.c_str());
      lua_setfield(state, -2, "entity_id");
    }

    const int tableRef = luaL_ref(state, LUA_REGISTRYINDEX);
    scripts_.push_back(ScriptInstance{
      .entityId = std::move(entityId),
      .module = module,
      .path = scriptPath,
      .lastWriteTime = luaScriptWriteTime(scriptPath),
      .tableRef = tableRef,
    });
    return true;
  };

  if (!project.scriptEntry.empty() && !loadScript({}, project.scriptEntry, "Lua project script")) {
    return false;
  }

  for (Entity& entity : world.entities) {
    if (!entity.luaScript.has_value()) {
      continue;
    }

    const std::size_t scriptIndex = scripts_.size();
    if (!loadScript(entity.id, entity.luaScript->module, "Lua script")) {
      return false;
    }

    lua_rawgeti(state, LUA_REGISTRYINDEX, scripts_[scriptIndex].tableRef);
    lua_pushnumber(state, entity.luaScript->speed);
    lua_setfield(state, -2, "speed");
    lua_pushnumber(state, entity.luaScript->jumpSpeed);
    lua_setfield(state, -2, "jump_speed");
    lua_pop(state, 1);
  }

  for (const HudButtonElement& button : world.hudButtons) {
    if (button.script.empty()) {
      continue;
    }

    const std::size_t scriptIndex = scripts_.size();
    if (!loadScript(button.id, button.script, "HUD button Lua script")) {
      return false;
    }

    lua_rawgeti(state, LUA_REGISTRYINDEX, scripts_[scriptIndex].tableRef);
    lua_pushstring(state, button.id.c_str());
    lua_setfield(state, -2, "ui_id");
    lua_pop(state, 1);
  }

  return true;
#endif
}

void LuaScriptHost::start() {
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr) {
    return;
  }
  for (const ScriptInstance& script : scripts_) {
    luaCallLifecycle(state, script.tableRef, "on_create", script.path, script.entityId);
    luaCallLifecycle(state, script.tableRef, "on_start", script.path, script.entityId);
  }
#endif
}

void LuaScriptHost::update(const float dt) {
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr) {
    return;
  }

  lua_getglobal(state, "Time");
  if (lua_istable(state, -1)) {
    lua_pushnumber(state, dt);
    lua_setfield(state, -2, "delta_time");
  }
  lua_pop(state, 1);

  reloadChangedScripts();
  dispatchHudEvents();
  updateTimers(dt);

  for (const ScriptInstance& script : scripts_) {
    luaCallLifecycle(state, script.tableRef, "on_update", script.path, script.entityId, dt);
  }
#else
  (void)dt;
#endif
}

void LuaScriptHost::fixedUpdate(const float dt) {
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr) {
    return;
  }
  for (const ScriptInstance& script : scripts_) {
    luaCallLifecycle(state, script.tableRef, "on_fixed_update", script.path, script.entityId, dt);
  }
#else
  (void)dt;
#endif
}

void LuaScriptHost::destroy() {
  unloadScripts();
}

void LuaScriptHost::reloadChangedScripts() {
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr || world_ == nullptr) {
    return;
  }

  for (ScriptInstance& script : scripts_) {
    const std::filesystem::file_time_type currentWriteTime = luaScriptWriteTime(script.path);
    if (currentWriteTime == std::filesystem::file_time_type{} || currentWriteTime == script.lastWriteTime) {
      continue;
    }

    std::string error;
    if (!luaLoadScriptTable(state, script.path, error)) {
      std::cerr << "Lua hot reload failed for " << script.path.string() << ": " << error << '\n';
      script.lastWriteTime = currentWriteTime;
      continue;
    }

    if (!script.entityId.empty()) {
      lua_pushstring(state, script.entityId.c_str());
      lua_setfield(state, -2, "entity_id");
      if (const Entity* entity = findEntity(*world_, script.entityId); entity != nullptr && entity->luaScript.has_value()) {
        lua_pushnumber(state, entity->luaScript->speed);
        lua_setfield(state, -2, "speed");
        lua_pushnumber(state, entity->luaScript->jumpSpeed);
        lua_setfield(state, -2, "jump_speed");
      } else {
        lua_pushstring(state, script.entityId.c_str());
        lua_setfield(state, -2, "ui_id");
      }
    }

    const int newTableRef = luaL_ref(state, LUA_REGISTRYINDEX);
    luaCallLifecycle(state, script.tableRef, "on_destroy", script.path, script.entityId);
    luaL_unref(state, LUA_REGISTRYINDEX, script.tableRef);
    script.tableRef = newTableRef;
    script.lastWriteTime = currentWriteTime;
    luaCallLifecycle(state, script.tableRef, "on_create", script.path, script.entityId);
    luaCallLifecycle(state, script.tableRef, "on_start", script.path, script.entityId);
    std::cout << "Lua hot reloaded: " << script.path.string() << '\n';
  }
#endif
}

void LuaScriptHost::unloadScripts() {
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr) {
    scripts_.clear();
    timers_.clear();
    eventSubscriptions_.clear();
    return;
  }
  for (const ScriptInstance& script : scripts_) {
    luaCallLifecycle(state, script.tableRef, "on_destroy", script.path, script.entityId);
    luaL_unref(state, LUA_REGISTRYINDEX, script.tableRef);
  }
  scripts_.clear();
  clearTimersAndEvents();
#endif
}

void LuaScriptHost::requestSceneLoad(const std::string& sceneId) {
  pendingSceneLoad_ = sceneId;
}

bool LuaScriptHost::hasPendingSceneLoad() const {
  return pendingSceneLoad_.has_value();
}

bool LuaScriptHost::applyPendingSceneLoad(std::string& error) {
  if (!pendingSceneLoad_.has_value()) {
    return false;
  }
  const std::string sceneId = std::move(*pendingSceneLoad_);
  pendingSceneLoad_.reset();

#if !DEMI_HAS_LUA54
  (void)sceneId;
  error = "Lua 5.4 support is unavailable.";
  return false;
#else
  if (project_ == nullptr || world_ == nullptr) {
    error = "Scene load requested before runtime was initialized.";
    return false;
  }

  std::optional<World> newWorld = loadScene(*project_, sceneId, error);
  if (!newWorld.has_value()) {
    return false;
  }

  unloadScripts();
  *world_ = std::move(*newWorld);

  std::string scriptError;
  if (!loadWorldScripts(*project_, *world_, scriptError)) {
    error = "Scene loaded but scripts failed: " + scriptError;
    return false;
  }

  start();
  return true;
#endif
}

} // namespace demi::runtime
