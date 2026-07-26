#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"

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

void LuaScriptHost::requestSceneLoad(const std::string &sceneId) {
  pendingSceneLoad_ = sceneId;
}

bool LuaScriptHost::hasPendingSceneLoad() const {
  return pendingSceneLoad_.has_value();
}

bool LuaScriptHost::applyPendingSceneLoad(std::string &error) {
  if (!pendingSceneLoad_.has_value()) {
    return false;
  }
  const std::string sceneId = std::move(*pendingSceneLoad_);
  pendingSceneLoad_.reset();

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
}

} // namespace demi::runtime
