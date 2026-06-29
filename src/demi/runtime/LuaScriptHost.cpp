#include "demi/runtime/LuaScriptHost.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#if DEMI_HAS_LUA54
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#endif

namespace demi::runtime {

namespace {

std::string normalizedKey(std::string key) {
  std::ranges::transform(key, key.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return key;
}

#if DEMI_HAS_LUA54

int debugLog(lua_State* state) {
  const char* message = luaL_checkstring(state, 1);
  std::cout << "[lua] " << message << '\n';
  return 0;
}

LuaScriptHost* hostFromUpvalue(lua_State* state) {
  return static_cast<LuaScriptHost*>(lua_touserdata(state, lua_upvalueindex(1)));
}

int inputIsDown(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  const char* key = luaL_checkstring(state, 1);
  const bool isDown = host != nullptr && host->isKeyDown(key);
  lua_pushboolean(state, isDown);
  return 1;
}

int inputAxis(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  const char* negative = luaL_checkstring(state, 1);
  const char* positive = luaL_checkstring(state, 2);
  float value = 0.0F;
  if (host != nullptr && host->isKeyDown(negative)) {
    value -= 1.0F;
  }
  if (host != nullptr && host->isKeyDown(positive)) {
    value += 1.0F;
  }
  lua_pushnumber(state, value);
  return 1;
}

int inputVector(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  const char* left = luaL_checkstring(state, 1);
  const char* right = luaL_checkstring(state, 2);
  const char* down = luaL_checkstring(state, 3);
  const char* up = luaL_checkstring(state, 4);

  float x = 0.0F;
  float y = 0.0F;
  if (host != nullptr && host->isKeyDown(left)) {
    x -= 1.0F;
  }
  if (host != nullptr && host->isKeyDown(right)) {
    x += 1.0F;
  }
  if (host != nullptr && host->isKeyDown(down)) {
    y -= 1.0F;
  }
  if (host != nullptr && host->isKeyDown(up)) {
    y += 1.0F;
  }

  if (x != 0.0F && y != 0.0F) {
    x *= 0.70710678F;
    y *= 0.70710678F;
  }

  lua_pushnumber(state, x);
  lua_pushnumber(state, y);
  return 2;
}

int entityAddPosition(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* entityId = luaL_checkstring(state, 1);
  const float dx = static_cast<float>(luaL_checknumber(state, 2));
  const float dy = static_cast<float>(luaL_checknumber(state, 3));

  if (host != nullptr) {
    (void)host->addEntityPosition(entityId, dx, dy);
  }
  return 0;
}

int entityGetPosition(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* entityId = luaL_checkstring(state, 1);

  const std::optional<Vec2> position = host != nullptr ? host->entityPosition(entityId) : std::nullopt;
  if (!position.has_value()) {
    lua_pushnil(state);
    return 1;
  }

  lua_pushnumber(state, position->x);
  lua_pushnumber(state, position->y);
  return 2;
}

int entitySetPosition(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* entityId = luaL_checkstring(state, 1);
  const float x = static_cast<float>(luaL_checknumber(state, 2));
  const float y = static_cast<float>(luaL_checknumber(state, 3));
  const bool changed = host != nullptr && host->setEntityPosition(entityId, x, y);
  lua_pushboolean(state, changed);
  return 1;
}

int entityFind(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  const char* idOrName = luaL_checkstring(state, 1);
  const std::optional<std::string> id = host != nullptr ? host->findEntityId(idOrName) : std::nullopt;
  if (!id.has_value()) {
    lua_pushnil(state);
    return 1;
  }
  lua_pushstring(state, id->c_str());
  return 1;
}

int sceneLoad(lua_State* state) {
  const char* sceneId = luaL_checkstring(state, 1);
  std::cerr << "Scene.load is not implemented yet for " << sceneId << '\n';
  lua_pushboolean(state, false);
  return 1;
}

void callLifecycle(lua_State* state, const int tableRef, const char* functionName, const float dt = 0.0F) {
  lua_rawgeti(state, LUA_REGISTRYINDEX, tableRef);
  lua_getfield(state, -1, functionName);
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }

  lua_pushvalue(state, -2);
  int argCount = 1;
  if (functionName == std::string_view("on_update") || functionName == std::string_view("on_fixed_update")) {
    lua_pushnumber(state, dt);
    argCount = 2;
  }

  if (lua_pcall(state, argCount, 0, 0) != LUA_OK) {
    std::cerr << "Lua " << functionName << " failed: " << lua_tostring(state, -1) << '\n';
    lua_pop(state, 1);
  }
  lua_pop(state, 1);
}

std::filesystem::path resolveScriptPath(const ProjectData& project, const std::string& module) {
  constexpr std::string_view prefix = "script://";
  if (module.starts_with(prefix)) {
    return project.projectDirectory / module.substr(prefix.size());
  }
  return project.projectDirectory / module;
}

#endif

} // namespace

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

bool LuaScriptHost::initialize(World& world, const InputState& input, std::string& error) {
#if !DEMI_HAS_LUA54
  (void)world;
  (void)input;
  error = "Lua 5.4 support is unavailable because lua5.4 was not found at configure time.";
  return false;
#else
  world_ = &world;
  input_ = &input;

  auto* state = luaL_newstate();
  if (state == nullptr) {
    error = "Failed to allocate Lua state.";
    return false;
  }
  luaL_openlibs(state);

  lua_newtable(state);
  lua_pushcfunction(state, debugLog);
  lua_setfield(state, -2, "log");
  lua_setglobal(state, "Debug");

  lua_newtable(state);
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, inputIsDown, 1);
  lua_setfield(state, -2, "is_down");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, inputAxis, 1);
  lua_setfield(state, -2, "axis");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, inputVector, 1);
  lua_setfield(state, -2, "vector");
  lua_setglobal(state, "Input");

  lua_newtable(state);
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, entityFind, 1);
  lua_setfield(state, -2, "find");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, entityAddPosition, 1);
  lua_setfield(state, -2, "add_position");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, entitySetPosition, 1);
  lua_setfield(state, -2, "set_position");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, entityGetPosition, 1);
  lua_setfield(state, -2, "get_position");
  lua_setglobal(state, "Entity");

  lua_newtable(state);
  lua_pushnumber(state, 0.0);
  lua_setfield(state, -2, "delta_time");
  lua_setglobal(state, "Time");

  lua_newtable(state);
  lua_pushcfunction(state, sceneLoad);
  lua_setfield(state, -2, "load");
  lua_setglobal(state, "Scene");

  state_ = state;
  return true;
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

  for (Entity& entity : world.entities) {
    if (!entity.luaScript.has_value()) {
      continue;
    }

    const std::filesystem::path scriptPath = resolveScriptPath(project, entity.luaScript->module);
    if (luaL_dofile(state, scriptPath.string().c_str()) != LUA_OK) {
      error = "Failed to load Lua script " + scriptPath.string() + ": " + lua_tostring(state, -1);
      lua_pop(state, 1);
      return false;
    }

    if (!lua_istable(state, -1)) {
      lua_pop(state, 1);
      error = "Lua script must return a table: " + scriptPath.string();
      return false;
    }

    lua_pushstring(state, entity.id.c_str());
    lua_setfield(state, -2, "entity_id");
    lua_pushnumber(state, entity.luaScript->speed);
    lua_setfield(state, -2, "speed");

    const int tableRef = luaL_ref(state, LUA_REGISTRYINDEX);
    scripts_.push_back(ScriptInstance{.entityId = entity.id, .module = entity.luaScript->module, .tableRef = tableRef});
  }

  return true;
#endif
}

bool LuaScriptHost::isKeyDown(const std::string& key) const {
  return input_ != nullptr && input_->keysDown.contains(normalizedKey(key));
}

bool LuaScriptHost::addEntityPosition(const std::string& entityId, const float dx, const float dy) {
  if (world_ == nullptr) {
    return false;
  }

  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform2D.has_value()) {
    return false;
  }

  entity->transform2D->position.x += dx;
  entity->transform2D->position.y += dy;
  return true;
}

bool LuaScriptHost::setEntityPosition(const std::string& entityId, const float x, const float y) {
  if (world_ == nullptr) {
    return false;
  }

  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform2D.has_value()) {
    return false;
  }

  entity->transform2D->position = Vec2{.x = x, .y = y};
  return true;
}

std::optional<Vec2> LuaScriptHost::entityPosition(const std::string& entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }

  const Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform2D.has_value()) {
    return std::nullopt;
  }

  return entity->transform2D->position;
}

std::optional<std::string> LuaScriptHost::findEntityId(const std::string& idOrName) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }

  for (const Entity& entity : world_->entities) {
    if (entity.id == idOrName || entity.name == idOrName) {
      return entity.id;
    }
  }
  return std::nullopt;
}

void LuaScriptHost::start() {
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr) {
    return;
  }
  for (const ScriptInstance& script : scripts_) {
    callLifecycle(state, script.tableRef, "on_create");
    callLifecycle(state, script.tableRef, "on_start");
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

  for (const ScriptInstance& script : scripts_) {
    callLifecycle(state, script.tableRef, "on_update", dt);
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
    callLifecycle(state, script.tableRef, "on_fixed_update", dt);
  }
#else
  (void)dt;
#endif
}

void LuaScriptHost::destroy() {
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr) {
    return;
  }
  for (const ScriptInstance& script : scripts_) {
    callLifecycle(state, script.tableRef, "on_destroy");
    luaL_unref(state, LUA_REGISTRYINDEX, script.tableRef);
  }
  scripts_.clear();
#endif
}

} // namespace demi::runtime
