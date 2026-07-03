#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scripting/LuaScriptHostInternal.h"

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

void LuaScriptHost::setMediaSystem(MediaSystem* media) {
  media_ = media;
}

void LuaScriptHost::setNetworkSystem(NetworkSystem* network) {
  network_ = network;
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
  if (world_ == nullptr) {
    return;
  }
  for (Entity& entity : world_->entities) {
    if (entity.audioSource.has_value() && entity.audioSource->playOnStart && entity.audioSource->handle == 0) {
      entity.audioSource->handle = playAudioSource(entity.id);
    }
    if (entity.videoPlayer.has_value() && entity.videoPlayer->playOnStart && entity.videoPlayer->handle == 0) {
      entity.videoPlayer->handle = playVideoPlayer(entity.id);
    }
  }
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
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state != nullptr) {
    clearLuaBindingGlobals(state);
  }
#endif
}


} // namespace demi::runtime
