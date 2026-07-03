#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scripting/LuaScriptHostInternal.h"

namespace demi::runtime {

LuaScriptHost::LuaScriptHost() = default;

LuaScriptHost::~LuaScriptHost() {
  destroy();
  if (state_ != nullptr) {
    lua_close(static_cast<lua_State*>(state_));
    state_ = nullptr;
  }
}

bool LuaScriptHost::initialize(World& world, const InputState& input, AudioSystem* audio, std::string& error) {
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
}

void LuaScriptHost::setMediaSystem(MediaSystem* media) {
  media_ = media;
}

void LuaScriptHost::setNetworkSystem(NetworkSystem* network) {
  network_ = network;
}


void LuaScriptHost::start() {
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr) {
    return;
  }
  for (const ScriptInstance& script : scripts_) {
    luaCallLifecycle(state, script.tableRef, "on_create", script.path, script.entityId);
    luaCallLifecycle(state, script.tableRef, "on_start", script.path, script.entityId);
  }
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
}

void LuaScriptHost::fixedUpdate(const float dt) {
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr) {
    return;
  }
  for (const ScriptInstance& script : scripts_) {
    luaCallLifecycle(state, script.tableRef, "on_fixed_update", script.path, script.entityId, dt);
  }
}

void LuaScriptHost::destroy() {
  unloadScripts();
  auto* state = static_cast<lua_State*>(state_);
  if (state != nullptr) {
    clearLuaBindingGlobals(state);
  }
}


} // namespace demi::runtime
