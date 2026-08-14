#include "demi/runtime/assets/RuntimeAssetService.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scripting/LuaScriptHostInternal.h"
#include "demi/runtime/scripting/annotations/HandleActionAnnotation.h"
#include "demi/runtime/scripting/annotations/LuaModulePath.h"
#include "demi/runtime/scripting/annotations/OnEventAnnotation.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <utility>

namespace demi::runtime {

bool LuaScriptHost::loadScriptInstance(std::string entityId,
                                       const std::string &module,
                                       const char *context,
                                       std::string &error) {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr || project_ == nullptr) {
    error = "Lua runtime or project is not initialized.";
    return false;
  }
  const std::filesystem::path scriptPath =
      luaResolveScriptPath(*project_, module);
  std::string scriptError;
  if (!luaLoadScriptTable(state, scriptPath, scriptError)) {
    error = std::string("Failed to load ") + context + " " +
            scriptPath.string() + ": " + scriptError;
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
      .actionHandlers = HandleActionAnnotation::parse(scriptPath),
      .eventHandlers = OnEventAnnotation::parse(scriptPath),
  });
  return true;
}

bool LuaScriptHost::loadWorldScripts(const ProjectData &project, World &world,
                                     std::string &error) {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr) {
    error = "LuaScriptHost was not initialized.";
    return false;
  }

  projectDirectory_ = project.projectDirectory;
  networkContract_.reset();
  if (!project.networkContract.empty()) {
    if (assetRegistry_ == nullptr) {
      error = "Project declares a network contract without an asset registry.";
      return false;
    }
    NetworkContractLoadResult loadedContract =
        loadNetworkContract(*assetRegistry_, project.networkContract);
    if (!loadedContract.contract) {
      error = loadedContract.diagnostics.empty()
                  ? "Network contract could not be loaded."
                  : loadedContract.diagnostics.front().code + ": " +
                        loadedContract.diagnostics.front().message;
      return false;
    }
    networkContract_ = std::move(*loadedContract.contract);
  }
  applicationServices_.configureStorage(project.name, project.projectDirectory);
  if (project_ != &project) {
    prefabService_.configure(project.projectDirectory);
    sceneFlow_.configure(project);
    resourceLifetimes_.capture(world.activeSceneId, world.entities);
  }
  inputActions_ = project.inputActions;
  project_ = &project;
  luaConfigurePackagePath(state, project);
  moduleActionHandlers_.clear();

  for (const std::string &module : project.scriptModules) {
    const std::string scriptUri = LuaModulePath::scriptUri(module);
    const std::filesystem::path scriptPath =
        luaResolveScriptPath(project, scriptUri);
    moduleActionHandlers_.push_back(ModuleActionHandler{
        .module = LuaModulePath::moduleName(module),
        .path = scriptPath,
        .lastWriteTime = luaScriptWriteTime(scriptPath),
        .actionHandlers = HandleActionAnnotation::parse(scriptPath),
        .eventHandlers = OnEventAnnotation::parse(scriptPath),
    });
  }

  if (!project.scriptEntry.empty() &&
      !loadScriptInstance({}, project.scriptEntry, "Lua project script",
                          error)) {
    return false;
  }

  for (Entity &entity : world.entities) {
    if (!entity.hasComponent<LuaScriptComponent>()) {
      continue;
    }

    const std::size_t scriptIndex = scripts_.size();
    if (!loadScriptInstance(entity.id,
                            entity.component<LuaScriptComponent>()->module,
                            "Lua script", error)) {
      return false;
    }

    applyScriptProperties(
        state, scripts_[scriptIndex].tableRef,
        entity.component<LuaScriptComponent>()->propertiesJson);
  }

  for (const ui::UiNode &node : world.ui.nodes) {
    if ((node.type != "button" && node.type != "toggle" &&
         node.type != "text_input") ||
        node.script.empty()) {
      continue;
    }

    const std::size_t scriptIndex = scripts_.size();
    if (!loadScriptInstance(node.id, node.script, "HUD button Lua script",
                            error)) {
      return false;
    }

    lua_rawgeti(state, LUA_REGISTRYINDEX, scripts_[scriptIndex].tableRef);
    lua_pushstring(state, node.id.c_str());
    lua_setfield(state, -2, "ui_id");
    lua_pop(state, 1);
  }

  return true;
}

bool LuaScriptHost::loadDynamicEntityScript(const std::string &entityId,
                                            std::string &error) {
  auto *state = static_cast<lua_State *>(state_);
  Entity *entity = world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  if (state == nullptr || entity == nullptr ||
      !entity->hasComponent<LuaScriptComponent>()) {
    error = "Dynamic LuaScript entity was not found: " + entityId;
    return false;
  }
  const auto duplicate =
      std::ranges::find(scripts_, entityId, &ScriptInstance::entityId);
  if (duplicate != scripts_.end()) {
    error = "Entity already has a loaded Lua script: " + entityId;
    return false;
  }
  const std::size_t index = scripts_.size();
  if (!loadScriptInstance(entityId,
                          entity->component<LuaScriptComponent>()->module,
                          "dynamic Lua script", error)) {
    return false;
  }
  applyScriptProperties(
      state, scripts_[index].tableRef,
      entity->component<LuaScriptComponent>()->propertiesJson);
  startScriptInstance(scripts_[index]);
  return true;
}

bool LuaScriptHost::loadDynamicUiScript(const std::string &uiId,
                                        std::string &error) {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr || world_ == nullptr) {
    error = "Dynamic scripted UI node was not found: " + uiId;
    return false;
  }
  const auto found = std::ranges::find(world_->ui.nodes, uiId, &ui::UiNode::id);
  if (found == world_->ui.nodes.end() || found->script.empty()) {
    error = "Dynamic scripted UI node was not found: " + uiId;
    return false;
  }
  if (std::ranges::find(scripts_, uiId, &ScriptInstance::entityId) !=
      scripts_.end()) {
    error = "UI node already has a loaded Lua script: " + uiId;
    return false;
  }
  const std::size_t index = scripts_.size();
  if (!loadScriptInstance(uiId, found->script, "dynamic HUD Lua script", error))
    return false;
  lua_rawgeti(state, LUA_REGISTRYINDEX, scripts_[index].tableRef);
  lua_pushstring(state, uiId.c_str());
  lua_setfield(state, -2, "ui_id");
  lua_pop(state, 1);
  startScriptInstance(scripts_[index]);
  return true;
}

void LuaScriptHost::startScriptInstance(ScriptInstance &script) {
  if (script.lifecycleStarted)
    return;
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr)
    return;
  luaCallLifecycle(state, script.tableRef, "on_create", script.path,
                   script.entityId);
  luaCallLifecycle(state, script.tableRef, "on_start", script.path,
                   script.entityId);
  script.lifecycleStarted = true;
}

void LuaScriptHost::unloadEntityScript(const std::string &entityId) {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr)
    return;
  std::erase_if(scripts_, [&](const ScriptInstance &script) {
    if (script.entityId != entityId)
      return false;
    if (script.lifecycleStarted)
      luaCallLifecycle(state, script.tableRef, "on_destroy", script.path,
                       script.entityId);
    luaL_unref(state, LUA_REGISTRYINDEX, script.tableRef);
    return true;
  });
}
void LuaScriptHost::reloadChangedScripts() {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr || world_ == nullptr) {
    return;
  }

  for (ScriptInstance &script : scripts_) {
    const std::filesystem::file_time_type currentWriteTime =
        luaScriptWriteTime(script.path);
    if (currentWriteTime == std::filesystem::file_time_type{} ||
        currentWriteTime == script.lastWriteTime) {
      continue;
    }

    std::string error;
    if (!luaLoadScriptTable(state, script.path, error)) {
      std::cerr << "Lua hot reload failed for " << script.path.string() << ": "
                << error << '\n';
      script.lastWriteTime = currentWriteTime;
      continue;
    }

    if (!script.entityId.empty()) {
      lua_pushstring(state, script.entityId.c_str());
      lua_setfield(state, -2, "entity_id");
      if (const Entity *entity = findEntity(*world_, script.entityId);
          entity != nullptr && entity->hasComponent<LuaScriptComponent>()) {
        (void)entity;
      } else {
        lua_pushstring(state, script.entityId.c_str());
        lua_setfield(state, -2, "ui_id");
      }
    }

    const int newTableRef = luaL_ref(state, LUA_REGISTRYINDEX);
    luaCallLifecycle(state, script.tableRef, "on_destroy", script.path,
                     script.entityId);
    luaL_unref(state, LUA_REGISTRYINDEX, script.tableRef);
    script.tableRef = newTableRef;
    if (const Entity *entity = findEntity(*world_, script.entityId);
        entity != nullptr && entity->hasComponent<LuaScriptComponent>()) {
      applyScriptProperties(
          state, script.tableRef,
          entity->component<LuaScriptComponent>()->propertiesJson);
    }
    script.actionHandlers = HandleActionAnnotation::parse(script.path);
    script.eventHandlers = OnEventAnnotation::parse(script.path);
    script.lastWriteTime = currentWriteTime;
    luaCallLifecycle(state, script.tableRef, "on_create", script.path,
                     script.entityId);
    luaCallLifecycle(state, script.tableRef, "on_start", script.path,
                     script.entityId);
    std::cout << "Lua hot reloaded: " << script.path.string() << '\n';
  }
}

void LuaScriptHost::unloadScripts() {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr) {
    scripts_.clear();
    timers_.clear();
    eventSubscriptions_.clear();
    saveMigrationHooks_.clear();
    return;
  }
  for (const ScriptInstance &script : scripts_) {
    if (script.lifecycleStarted)
      luaCallLifecycle(state, script.tableRef, "on_destroy", script.path,
                       script.entityId);
    luaL_unref(state, LUA_REGISTRYINDEX, script.tableRef);
  }
  scripts_.clear();
  clearTimersAndEvents();
  clearSaveMigrationHooks();
}

bool LuaScriptHost::requestSceneLoad(const std::string &sceneId) {
  const bool accepted = sceneFlow_.prepare(sceneId, false);
  if (!accepted)
    return false;
  preparedSceneAssetRequest_ = 0;
  preparedSceneAssetId_.clear();
  sceneAssetError_.clear();
  if (runtimeAssets_ != nullptr && !activeSceneAssetGroups_.contains(sceneId)) {
    Diagnostics diagnostics;
    preparedSceneAssetRequest_ =
        runtimeAssets_->prepareScene(sceneId, &diagnostics);
    if (hasErrors(diagnostics)) {
      sceneAssetError_ = diagnostics.front().message;
      (void)sceneFlow_.cancel();
      return false;
    }
    preparedSceneAssetId_ = sceneId;
  }
  autoActivatePrepared_ = true;
  return true;
}

bool LuaScriptHost::prepareScene(const std::string &sceneId,
                                 const bool additive) {
  autoActivatePrepared_ = false;
  if (!sceneFlow_.prepare(sceneId, additive))
    return false;
  preparedSceneAssetRequest_ = 0;
  preparedSceneAssetId_.clear();
  sceneAssetError_.clear();
  if (runtimeAssets_ != nullptr && !activeSceneAssetGroups_.contains(sceneId)) {
    Diagnostics diagnostics;
    preparedSceneAssetRequest_ =
        runtimeAssets_->prepareScene(sceneId, &diagnostics);
    if (hasErrors(diagnostics)) {
      sceneAssetError_ = diagnostics.front().message;
      (void)sceneFlow_.cancel();
      return false;
    }
    preparedSceneAssetId_ = sceneId;
  }
  return true;
}

bool LuaScriptHost::cancelScenePreparation() {
  autoActivatePrepared_ = false;
  pendingPreparedActivation_ = false;
  if (runtimeAssets_ != nullptr && preparedSceneAssetRequest_ != 0)
    (void)runtimeAssets_->cancel(preparedSceneAssetRequest_);
  preparedSceneAssetRequest_ = 0;
  preparedSceneAssetId_.clear();
  sceneAssetError_.clear();
  return sceneFlow_.cancel();
}

float LuaScriptHost::scenePreparationProgress() {
  sceneFlow_.poll();
  if (runtimeAssets_ == nullptr || preparedSceneAssetRequest_ == 0)
    return sceneFlow_.progress();
  const auto assetProgress =
      runtimeAssets_->progress(preparedSceneAssetRequest_);
  return std::min(sceneFlow_.progress(),
                  static_cast<float>(assetProgress.fraction));
}

bool LuaScriptHost::scenePrepared() {
  sceneFlow_.poll();
  if (sceneFlow_.state() != ScenePreparationState::Ready)
    return false;
  if (runtimeAssets_ == nullptr || preparedSceneAssetRequest_ == 0)
    return true;
  const auto assetProgress =
      runtimeAssets_->progress(preparedSceneAssetRequest_);
  if (assetProgress.stage == assets::AssetGroupStage::Failed) {
    sceneAssetError_ = assetProgress.error;
    return false;
  }
  return assetProgress.stage == assets::AssetGroupStage::Ready;
}

bool LuaScriptHost::requestPreparedSceneActivation() {
  if (!scenePrepared())
    return false;
  pendingPreparedActivation_ = true;
  return true;
}

bool LuaScriptHost::requestSceneUnload(const std::string &sceneId) {
  if (world_ == nullptr || sceneId.empty() ||
      !world_->loadedSceneIds.contains(sceneId))
    return false;
  pendingSceneUnload_ = sceneId;
  return true;
}

bool LuaScriptHost::requestSceneReload() {
  if (world_ == nullptr || world_->activeSceneId.empty())
    return false;
  if (!sceneFlow_.prepare(world_->activeSceneId, false))
    return false;
  preparedSceneAssetRequest_ = 0;
  preparedSceneAssetId_.clear();
  sceneAssetError_.clear();
  autoActivatePrepared_ = true;
  return true;
}

bool LuaScriptHost::setEntityPersistent(const std::string &entityId,
                                        const bool persistent) {
  return world_ != nullptr &&
         sceneFlow_.setPersistent(*world_, entityId, persistent);
}

std::string LuaScriptHost::activeSceneId() const {
  return world_ == nullptr ? std::string{} : world_->activeSceneId;
}

std::string LuaScriptHost::sceneFlowError() const {
  return sceneAssetError_.empty() ? sceneFlow_.error() : sceneAssetError_;
}

bool LuaScriptHost::hasPendingSceneLoad() {
  sceneFlow_.poll();
  bool assetsFailed = false;
  bool assetsReady = preparedSceneAssetRequest_ == 0;
  if (runtimeAssets_ != nullptr && preparedSceneAssetRequest_ != 0) {
    const auto progress = runtimeAssets_->progress(preparedSceneAssetRequest_);
    assetsFailed = progress.stage == assets::AssetGroupStage::Failed;
    assetsReady = progress.stage == assets::AssetGroupStage::Ready;
    if (assetsFailed)
      sceneAssetError_ = progress.error;
  }
  if (autoActivatePrepared_ && assetsReady &&
      sceneFlow_.state() == ScenePreparationState::Ready)
    pendingPreparedActivation_ = true;
  return pendingPreparedActivation_ || pendingSceneUnload_.has_value() ||
         (autoActivatePrepared_ && assetsFailed) ||
         (autoActivatePrepared_ &&
          sceneFlow_.state() == ScenePreparationState::Failed);
}

bool LuaScriptHost::applyPendingSceneLoad(std::string &error) {
  if (project_ == nullptr || world_ == nullptr) {
    error = "Scene load requested before runtime was initialized.";
    return false;
  }

  if (sceneFlow_.state() == ScenePreparationState::Failed) {
    error = sceneFlow_.error();
    autoActivatePrepared_ = false;
    return false;
  }
  if (!sceneAssetError_.empty()) {
    error = sceneAssetError_;
    autoActivatePrepared_ = false;
    (void)sceneFlow_.cancel();
    preparedSceneAssetRequest_ = 0;
    preparedSceneAssetId_.clear();
    return false;
  }

  if (pendingSceneUnload_) {
    const std::string unloadingScene = *pendingSceneUnload_;
    (void)emitEvent("scene_unloading", 0);
    flushWorldCommands();
    std::vector<std::string> unloadingScripts;
    for (const Entity &entity : world_->entities)
      if (entity.sceneOwner == *pendingSceneUnload_ && !entity.persistent)
        unloadingScripts.push_back(entity.id);
    for (const ui::UiNode &node : world_->ui.nodes)
      if (node.sceneOwner == *pendingSceneUnload_)
        unloadingScripts.push_back(node.id);
    for (const std::string &ownerId : unloadingScripts)
      unloadEntityScript(ownerId);
    // Destruction callbacks cannot enqueue work into a scene whose lifetime
    // has already ended.
    worldCommands_.clear();
    const auto transition =
        sceneFlow_.unload(*world_, *pendingSceneUnload_, resourceLifetimes_);
    prefabService_.prune(*world_);
    pendingSceneUnload_.reset();
    if (!transition) {
      error = "Scene unload failed.";
      return false;
    }
    if (runtimeAssets_ != nullptr &&
        activeSceneAssetGroups_.erase(unloadingScene) > 0) {
      Diagnostics diagnostics;
      if (!runtimeAssets_->releaseScene(unloadingScene, &diagnostics)) {
        error = diagnostics.empty() ? "Scene asset release failed."
                                    : diagnostics.front().message;
        return false;
      }
    }
    (void)emitEvent("scene_unloaded", 0);
    (void)emitEvent("active_scene_changed", 0);
    return true;
  }

  if (!pendingPreparedActivation_)
    return false;
  pendingPreparedActivation_ = false;
  autoActivatePrepared_ = false;
  if (const std::string activationFailure = sceneFlow_.activationError(*world_);
      !activationFailure.empty()) {
    error = activationFailure;
    return false;
  }
  const bool additive = sceneFlow_.preparedAdditive();
  bool activatedAssets = false;
  if (runtimeAssets_ != nullptr && preparedSceneAssetRequest_ != 0) {
    Diagnostics diagnostics;
    if (!runtimeAssets_->activate(preparedSceneAssetRequest_, &diagnostics)) {
      error = diagnostics.empty() ? "Prepared scene assets could not activate."
                                  : diagnostics.front().message;
      return false;
    }
    activatedAssets = true;
  }
  if (!additive) {
    (void)emitEvent("scene_unloading", 0);
    flushWorldCommands();
    // Destroy callbacks still see the outgoing world and its entities.
    unloadScripts();
    worldCommands_.clear();
  }
  const auto transition = sceneFlow_.activate(*world_, resourceLifetimes_);
  prefabService_.prune(*world_);
  if (!transition) {
    if (activatedAssets && runtimeAssets_ != nullptr) {
      Diagnostics diagnostics;
      (void)runtimeAssets_->releaseScene(preparedSceneAssetId_, &diagnostics);
    }
    error = "Prepared scene activation failed.";
    return false;
  }
  if (activatedAssets)
    activeSceneAssetGroups_.insert(preparedSceneAssetId_);
  preparedSceneAssetRequest_ = 0;
  preparedSceneAssetId_.clear();
  sceneAssetError_.clear();
  if (!transition->additive && runtimeAssets_ != nullptr) {
    std::vector<std::string> outgoingGroups;
    for (const std::string &activeScene : activeSceneAssetGroups_)
      if (activeScene != transition->activeScene)
        outgoingGroups.push_back(activeScene);
    for (const std::string &outgoingScene : outgoingGroups) {
      Diagnostics diagnostics;
      if (!runtimeAssets_->releaseScene(outgoingScene, &diagnostics)) {
        error = diagnostics.empty() ? "Outgoing scene assets could not release."
                                    : diagnostics.front().message;
        return false;
      }
      activeSceneAssetGroups_.erase(outgoingScene);
    }
  }
  if (transition->additive) {
    for (const std::string &entityId : transition->loadedEntities) {
      const Entity *entity = findEntity(*world_, entityId);
      if (entity == nullptr || !entity->enabled ||
          !entity->hasComponent<LuaScriptComponent>())
        continue;
      std::string scriptError;
      if (!loadDynamicEntityScript(entityId, scriptError)) {
        error = scriptError;
        return false;
      }
    }
    for (const std::string &uiId : transition->loadedUiNodes) {
      const auto node =
          std::ranges::find(world_->ui.nodes, uiId, &ui::UiNode::id);
      if (node == world_->ui.nodes.end() || node->script.empty())
        continue;
      std::string scriptError;
      if (!loadDynamicUiScript(uiId, scriptError)) {
        error = scriptError;
        return false;
      }
    }
  } else {
    std::string scriptError;
    if (!loadWorldScripts(*project_, *world_, scriptError)) {
      error = "Scene loaded but scripts failed: " + scriptError;
      return false;
    }
    start();
  }
  (void)emitEvent("scene_loaded", 0);
  (void)emitEvent("active_scene_changed", 0);
  return true;
}

} // namespace demi::runtime
