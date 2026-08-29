#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/assets/RuntimeAssetService.h"
#include "demi/runtime/scene/WorldQueries.h"
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

bool LuaScriptHost::initialize(World &world, InputState &input,
                               AudioSystem *audio, std::string &error) {
  world_ = &world;
  isoGridApi_.attach(&world);
  tilemapRuntime_.attach(&world, nullptr, &navigationGrid2D_);
  input_ = &input;
  activeInputContexts_ = {"gameplay"};
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
  luaInstallRuntimeLogging(state, runtimeLog_);

  return luaRegisterBindings(*this, state, error);
}

void LuaScriptHost::setMediaSystem(MediaSystem *media) { media_ = media; }

void LuaScriptHost::setNetworkSystem(NetworkSystem *network) {
  network_ = network;
}

void LuaScriptHost::setRuntimeAssetService(RuntimeAssetService *assets) {
  runtimeAssets_ = assets;
}

void LuaScriptHost::adoptActiveSceneAssetGroup(std::string sceneId) {
  if (!sceneId.empty())
    activeSceneAssetGroups_.insert(std::move(sceneId));
}

RuntimeAssetService *LuaScriptHost::runtimeAssetService() const {
  return runtimeAssets_;
}

void LuaScriptHost::setHotReloadEnabled(const bool enabled) {
  hotReloadEnabled_ = enabled;
}

bool LuaScriptHost::hotReloadEnabled() const { return hotReloadEnabled_; }

void LuaScriptHost::setAssetRegistry(const demi::AssetRegistry *assets) {
  assetRegistry_ = assets;
  tilemapRuntime_.attach(world_, assets, &navigationGrid2D_);
  if (assets == nullptr)
    return;
  const Diagnostics diagnostics = dataAssetStore_.replace(*assets);
  if (hasErrors(diagnostics))
    return;
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr)
    return;
  for (const DataAssetReloadEvent &event : dataAssetStore_.reloadEvents()) {
    lua_newtable(state);
    lua_pushstring(state, event.id.c_str());
    lua_setfield(state, -2, "id");
    lua_pushinteger(state, static_cast<lua_Integer>(event.oldRevision));
    lua_setfield(state, -2, "old_revision");
    lua_pushinteger(state, static_cast<lua_Integer>(event.newRevision));
    lua_setfield(state, -2, "new_revision");
    lua_newtable(state);
    for (std::size_t index = 0; index < event.affectedDependents.size();
         ++index) {
      lua_pushstring(state, event.affectedDependents[index].c_str());
      lua_rawseti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    lua_setfield(state, -2, "affected_dependents");
    (void)emitEvent("data_asset_reloaded", lua_gettop(state));
    lua_pop(state, 1);
  }
}

const NetworkContract *LuaScriptHost::networkContract() const {
  return networkContract_ ? &*networkContract_ : nullptr;
}

DataAssetStore &LuaScriptHost::dataAssetStore() { return dataAssetStore_; }
const DataAssetStore &LuaScriptHost::dataAssetStore() const {
  return dataAssetStore_;
}

navigation::NavigationGrid2D &LuaScriptHost::navigationGrid2D() {
  return navigationGrid2D_;
}

TilemapRuntime &LuaScriptHost::tilemapRuntime() { return tilemapRuntime_; }

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
  for (ScriptInstance &script : scripts_) {
    if (!script.entityId.empty()) {
      const Entity *entity =
          world_ == nullptr ? nullptr : findEntity(*world_, script.entityId);
      if (entity != nullptr && !entity->enabled)
        continue;
    }
    startScriptInstance(script);
  }
  if (world_ == nullptr) {
    return;
  }
  for (Entity &entity : world_->entities) {
    if (!entity.enabled)
      continue;
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
  flushWorldCommands();
}

void LuaScriptHost::update(const float dt) {
  ProfileScope updateScope("LuaScriptHost.update");
  if (runtimeAssets_ != nullptr)
    runtimeAssets_->update();
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
    ProfileScope scope("Lua.dispatch_animation_collision_events");
    dispatchAnimationCollisionEvents();
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
    if (!script.entityId.empty()) {
      const Entity *entity =
          world_ == nullptr ? nullptr : findEntity(*world_, script.entityId);
      if (entity != nullptr && !entity->enabled)
        continue;
    }
    ProfileScope scope("Lua.on_update");
    luaCallLifecycle(state, script.tableRef, "on_update", script.path,
                     script.entityId, dt);
  }
  flushWorldCommands();
}

void LuaScriptHost::fixedUpdate(const float dt) {
  ProfileScope fixedUpdateScope("LuaScriptHost.fixed_update");
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr) {
    return;
  }
  for (const ScriptInstance &script : scripts_) {
    if (!script.entityId.empty()) {
      const Entity *entity =
          world_ == nullptr ? nullptr : findEntity(*world_, script.entityId);
      if (entity != nullptr && !entity->enabled)
        continue;
    }
    ProfileScope scope("Lua.on_fixed_update");
    luaCallLifecycle(state, script.tableRef, "on_fixed_update", script.path,
                     script.entityId, dt);
  }
  flushWorldCommands();
}

void LuaScriptHost::destroy() {
  unloadScripts();
  if (world_ != nullptr) {
    for (auto &[_, recycler] : world_->uiVirtualRecyclers)
      recycler->clear(world_->ui, world_->uiTweens);
    world_->uiVirtualRecyclers.clear();
  }
  auto *state = static_cast<lua_State *>(state_);
  if (state != nullptr) {
    clearLuaBindingGlobals(state);
  }
}

} // namespace demi::runtime
