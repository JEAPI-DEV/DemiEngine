#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scripting/LuaScriptHostInternal.h"

#include <cstdlib>
#include <string>

namespace demi::runtime {

LuaScriptHost::LuaScriptHost() = default;

LuaScriptHost::~LuaScriptHost() {
  destroy();
  if (state_ != nullptr) {
    lua_close(static_cast<lua_State *>(state_));
    state_ = nullptr;
  }
}

bool LuaScriptHost::initialize(World &world, const InputState &input,
                               AudioSystem *audio, std::string &error) {
  world_ = &world;
  isoGridApi_.attach(&world);
  input_ = &input;
  audio_ = audio;
  const char *hotReload = std::getenv("DEMI_LUA_HOT_RELOAD");
  hotReloadEnabled_ = hotReload != nullptr && std::string(hotReload) != "0";

  auto *state = luaL_newstate();
  if (state == nullptr) {
    error = "Failed to allocate Lua state.";
    return false;
  }
  luaL_openlibs(state);
  state_ = state;

  return luaRegisterBindings(*this, state, error);
}

void LuaScriptHost::setMediaSystem(MediaSystem *media) { media_ = media; }

void LuaScriptHost::setNetworkSystem(NetworkSystem *network) {
  network_ = network;
}

std::filesystem::path
LuaScriptHost::resolveProjectPath(const std::string &path) const {
  const std::filesystem::path value(path);
  return value.is_absolute() ? value : projectDirectory_ / value;
}

void LuaScriptHost::start() {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr) {
    return;
  }
  for (const ScriptInstance &script : scripts_) {
    luaCallLifecycle(state, script.tableRef, "on_create", script.path,
                     script.entityId);
    luaCallLifecycle(state, script.tableRef, "on_start", script.path,
                     script.entityId);
  }
  if (world_ == nullptr) {
    return;
  }
  for (Entity &entity : world_->entities) {
    if (entity.hasComponent<AudioSourceComponent>() &&
        entity.component<AudioSourceComponent>()->playOnStart &&
        entity.component<AudioSourceComponent>()->handle == 0) {
      entity.component<AudioSourceComponent>()->handle =
          playAudioSource(entity.id);
    }
    if (entity.hasComponent<VideoPlayerComponent>() &&
        entity.component<VideoPlayerComponent>()->playOnStart &&
        entity.component<VideoPlayerComponent>()->handle == 0) {
      entity.component<VideoPlayerComponent>()->handle =
          playVideoPlayer(entity.id);
    }
  }
}

void LuaScriptHost::update(const float dt) {
  ProfileScope updateScope("LuaScriptHost.update");
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr) {
    return;
  }

  lua_getglobal(state, "Time");
  if (lua_istable(state, -1)) {
    lua_pushnumber(state, dt);
    lua_setfield(state, -2, "delta_time");
  }
  lua_pop(state, 1);

  if (hotReloadEnabled_) {
    ProfileScope scope("Lua.reload_changed_scripts");
    reloadChangedScripts();
  }
  {
    ProfileScope scope("Lua.dispatch_animation_events");
    dispatchAnimationEvents();
  }
  {
    ProfileScope scope("Lua.dispatch_physics_events");
    dispatchPhysicsEvents();
  }
  {
    ProfileScope scope("Lua.dispatch_hud_events");
    dispatchHudEvents();
  }
  {
    ProfileScope scope("Lua.update_timers");
    updateTimers(dt);
  }

  for (const ScriptInstance &script : scripts_) {
    ProfileScope scope("Lua.on_update");
    luaCallLifecycle(state, script.tableRef, "on_update", script.path,
                     script.entityId, dt);
  }
}

void LuaScriptHost::fixedUpdate(const float dt) {
  ProfileScope fixedUpdateScope("LuaScriptHost.fixed_update");
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr) {
    return;
  }
  for (const ScriptInstance &script : scripts_) {
    ProfileScope scope("Lua.on_fixed_update");
    luaCallLifecycle(state, script.tableRef, "on_fixed_update", script.path,
                     script.entityId, dt);
  }
}

void LuaScriptHost::destroy() {
  unloadScripts();
  auto *state = static_cast<lua_State *>(state_);
  if (state != nullptr) {
    clearLuaBindingGlobals(state);
  }
}

} // namespace demi::runtime
