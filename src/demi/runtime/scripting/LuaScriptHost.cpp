#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scripting/LuaScriptHostInternal.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <optional>
#include <utility>

namespace demi::runtime {

#if DEMI_HAS_LUA54
namespace {

void pushJsonToLua(lua_State* state, const nlohmann::json& value) {
  if (value.is_boolean()) {
    lua_pushboolean(state, value.get<bool>() ? 1 : 0);
    return;
  }
  if (value.is_number()) {
    lua_pushnumber(state, value.get<double>());
    return;
  }
  if (value.is_string()) {
    const std::string text = value.get<std::string>();
    lua_pushlstring(state, text.c_str(), text.size());
    return;
  }
  if (value.is_array()) {
    lua_newtable(state);
    int index = 1;
    for (const nlohmann::json& item : value) {
      pushJsonToLua(state, item);
      lua_rawseti(state, -2, index++);
    }
    return;
  }
  if (value.is_object()) {
    lua_newtable(state);
    for (const auto& [key, item] : value.items()) {
      pushJsonToLua(state, item);
      lua_setfield(state, -2, key.c_str());
    }
    return;
  }
  lua_pushnil(state);
}

void applyScriptProperties(lua_State* state, const int tableRef, const std::string& propertiesJson) {
  if (propertiesJson.empty()) {
    return;
  }

  nlohmann::json properties;
  try {
    properties = nlohmann::json::parse(propertiesJson);
  } catch (...) {
    return;
  }
  if (!properties.is_object()) {
    return;
  }

  lua_rawgeti(state, LUA_REGISTRYINDEX, tableRef);
  for (const auto& [key, value] : properties.items()) {
    pushJsonToLua(state, value);
    lua_setfield(state, -2, key.c_str());
  }
  lua_pop(state, 1);
}

std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::optional<std::string> handleActionAnnotation(const std::string& line) {
  const std::string marker = "-- @HandleAction(";
  const std::size_t markerPos = line.find(marker);
  if (markerPos == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t firstQuote = line.find('"', markerPos + marker.size());
  if (firstQuote == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t secondQuote = line.find('"', firstQuote + 1);
  if (secondQuote == std::string::npos) {
    return std::nullopt;
  }
  return line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

std::optional<std::string> onEventAnnotation(const std::string& line) {
  const std::string marker = "-- @OnEvent(";
  const std::size_t markerPos = line.find(marker);
  if (markerPos == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t firstQuote = line.find('"', markerPos + marker.size());
  if (firstQuote == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t secondQuote = line.find('"', firstQuote + 1);
  if (secondQuote == std::string::npos) {
    return std::nullopt;
  }
  return line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

std::optional<std::string> annotatedFunctionName(const std::string& line) {
  constexpr std::string_view prefix = "function ";
  const std::string text = trim(line);
  if (!text.starts_with(prefix)) {
    return std::nullopt;
  }
  const std::size_t nameStart = prefix.size();
  const std::size_t paren = text.find('(', nameStart);
  if (paren == std::string::npos) {
    return std::nullopt;
  }
  std::string name = trim(text.substr(nameStart, paren - nameStart));
  const std::size_t separator = name.find_last_of(".:");
  if (separator != std::string::npos) {
    name = name.substr(separator + 1);
  }
  if (name.empty()) {
    return std::nullopt;
  }
  return name;
}

std::vector<LuaActionHandler> parseActionHandlers(const std::filesystem::path& path) {
  std::vector<LuaActionHandler> handlers;
  std::ifstream input(path);
  if (!input) {
    return handlers;
  }

  std::vector<std::string> pendingActions;
  std::string line;
  while (std::getline(input, line)) {
    if (const std::optional<std::string> action = handleActionAnnotation(line)) {
      pendingActions.push_back(*action);
      continue;
    }

    if (pendingActions.empty()) {
      continue;
    }

    const std::string text = trim(line);
    if (text.empty() || text.starts_with("--")) {
      continue;
    }

    if (const std::optional<std::string> functionName = annotatedFunctionName(text)) {
      for (const std::string& action : pendingActions) {
        handlers.push_back({.action = action, .functionName = *functionName});
      }
    }
    pendingActions.clear();
  }
  return handlers;
}

std::vector<LuaEventHandler> parseEventHandlers(const std::filesystem::path& path) {
  std::vector<LuaEventHandler> handlers;
  std::ifstream input(path);
  if (!input) {
    return handlers;
  }

  std::vector<std::string> pendingEvents;
  std::string line;
  while (std::getline(input, line)) {
    if (const std::optional<std::string> eventName = onEventAnnotation(line)) {
      pendingEvents.push_back(*eventName);
      continue;
    }

    if (pendingEvents.empty()) {
      continue;
    }

    const std::string text = trim(line);
    if (text.empty() || text.starts_with("--")) {
      continue;
    }

    if (const std::optional<std::string> functionName = annotatedFunctionName(text)) {
      for (const std::string& eventName : pendingEvents) {
        handlers.push_back({.eventName = eventName, .functionName = *functionName});
      }
    }
    pendingEvents.clear();
  }
  return handlers;
}

std::string moduleNameFromProjectEntry(std::string module) {
  constexpr std::string_view scriptPrefix = "script://";
  if (module.starts_with(scriptPrefix)) {
    module = module.substr(scriptPrefix.size());
  }
  constexpr std::string_view scriptsPrefix = "scripts/";
  if (module.starts_with(scriptsPrefix)) {
    module = module.substr(scriptsPrefix.size());
  }
  constexpr std::string_view luaSuffix = ".lua";
  if (module.ends_with(luaSuffix)) {
    module.resize(module.size() - luaSuffix.size());
  }
  for (char& c : module) {
    if (c == '/' || c == '\\') {
      c = '.';
    }
  }
  return module;
}

std::string projectEntryToScriptUri(const std::string& module) {
  constexpr std::string_view scriptPrefix = "script://";
  if (module.starts_with(scriptPrefix)) {
    return module;
  }
  std::string path = module;
  for (char& c : path) {
    if (c == '.') {
      c = '/';
    }
  }
  return "script://scripts/" + path + ".lua";
}

void clearLuaBindingGlobals(lua_State* state) {
  constexpr const char* globals[] = {
    "Debug", "Input", "Entity", "Transform", "Transform3D", "Time", "Timer", "Events", "Scene", "Runtime",
    "Rigidbody2D", "Physics2D", "Hud", "Save", "Audio", "AudioSource", "Video", "Cutscene", "Network", "NetworkSession",
  };
  for (const char* global : globals) {
    lua_pushnil(state);
    lua_setglobal(state, global);
  }
  lua_gc(state, LUA_GCCOLLECT, 0);
}

} // namespace
#endif

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
  moduleActionHandlers_.clear();

  for (const std::string& module : project.scriptModules) {
    const std::string scriptUri = projectEntryToScriptUri(module);
    const std::filesystem::path scriptPath = luaResolveScriptPath(project, scriptUri);
    moduleActionHandlers_.push_back(ModuleActionHandler{
      .module = moduleNameFromProjectEntry(module),
      .path = scriptPath,
      .lastWriteTime = luaScriptWriteTime(scriptPath),
      .actionHandlers = parseActionHandlers(scriptPath),
      .eventHandlers = parseEventHandlers(scriptPath),
    });
  }

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
      .actionHandlers = parseActionHandlers(scriptPath),
      .eventHandlers = parseEventHandlers(scriptPath),
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

    applyScriptProperties(state, scripts_[scriptIndex].tableRef, entity.luaScript->propertiesJson);
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
        (void)entity;
      } else {
        lua_pushstring(state, script.entityId.c_str());
        lua_setfield(state, -2, "ui_id");
      }
    }

    const int newTableRef = luaL_ref(state, LUA_REGISTRYINDEX);
    luaCallLifecycle(state, script.tableRef, "on_destroy", script.path, script.entityId);
    luaL_unref(state, LUA_REGISTRYINDEX, script.tableRef);
    script.tableRef = newTableRef;
    if (const Entity* entity = findEntity(*world_, script.entityId); entity != nullptr && entity->luaScript.has_value()) {
      applyScriptProperties(state, script.tableRef, entity->luaScript->propertiesJson);
    }
    script.actionHandlers = parseActionHandlers(script.path);
    script.eventHandlers = parseEventHandlers(script.path);
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
