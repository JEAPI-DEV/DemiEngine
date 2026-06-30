#include "demi/runtime/LuaScriptHost.h"

#include "demi/runtime/Physics2D.h"

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

LuaScriptHost* hostFromUpvalue(lua_State* state);

int debugLog(lua_State* state) {
  const char* message = luaL_checkstring(state, 1);
  std::cout << "[lua] " << message << '\n';
  return 0;
}

int debugLine(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const float x1 = static_cast<float>(luaL_checknumber(state, 1));
  const float y1 = static_cast<float>(luaL_checknumber(state, 2));
  const float x2 = static_cast<float>(luaL_checknumber(state, 3));
  const float y2 = static_cast<float>(luaL_checknumber(state, 4));
  const float r = static_cast<float>(luaL_optnumber(state, 5, 1.0));
  const float g = static_cast<float>(luaL_optnumber(state, 6, 1.0));
  const float b = static_cast<float>(luaL_optnumber(state, 7, 1.0));
  const float a = static_cast<float>(luaL_optnumber(state, 8, 1.0));
  if (host != nullptr) {
    host->addDebugLine(x1, y1, x2, y2, r, g, b, a);
  }
  return 0;
}

int debugClearLines(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  if (host != nullptr) {
    host->clearDebugLines();
  }
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

int inputMouseDown(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  const char* button = luaL_checkstring(state, 1);
  lua_pushboolean(state, host != nullptr && host->isMouseDown(button));
  return 1;
}

int inputMousePosition(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  const Vec2 position = host != nullptr ? host->mousePosition() : Vec2{};
  lua_pushnumber(state, position.x);
  lua_pushnumber(state, position.y);
  return 2;
}

int inputMouseWorldPosition(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  const Vec2 position = host != nullptr ? host->mouseWorldPosition() : Vec2{};
  lua_pushnumber(state, position.x);
  lua_pushnumber(state, position.y);
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

int rigidbodyGetVelocity(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  const char* entityId = luaL_checkstring(state, 1);
  const std::optional<Vec2> velocity = host != nullptr ? host->getRigidbodyVelocity(entityId) : std::nullopt;
  if (!velocity.has_value()) {
    lua_pushnil(state);
    return 1;
  }
  lua_pushnumber(state, velocity->x);
  lua_pushnumber(state, velocity->y);
  return 2;
}

int rigidbodySetVelocity(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* entityId = luaL_checkstring(state, 1);
  const float x = static_cast<float>(luaL_checknumber(state, 2));
  const float y = static_cast<float>(luaL_checknumber(state, 3));
  const bool changed = host != nullptr && host->setRigidbodyVelocity(entityId, x, y);
  lua_pushboolean(state, changed);
  return 1;
}

int rigidbodySetVelocityX(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* entityId = luaL_checkstring(state, 1);
  const float x = static_cast<float>(luaL_checknumber(state, 2));
  const bool changed = host != nullptr && host->setRigidbodyVelocityX(entityId, x);
  lua_pushboolean(state, changed);
  return 1;
}

int rigidbodySetVelocityY(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* entityId = luaL_checkstring(state, 1);
  const float y = static_cast<float>(luaL_checknumber(state, 2));
  const bool changed = host != nullptr && host->setRigidbodyVelocityY(entityId, y);
  lua_pushboolean(state, changed);
  return 1;
}

int rigidbodyAddImpulse(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* entityId = luaL_checkstring(state, 1);
  const float x = static_cast<float>(luaL_checknumber(state, 2));
  const float y = static_cast<float>(luaL_checknumber(state, 3));
  const bool changed = host != nullptr && host->addRigidbodyImpulse(entityId, x, y);
  lua_pushboolean(state, changed);
  return 1;
}

int physicsOverlapBoxBinding(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  const float x = static_cast<float>(luaL_checknumber(state, 1));
  const float y = static_cast<float>(luaL_checknumber(state, 2));
  const float width = static_cast<float>(luaL_checknumber(state, 3));
  const float height = static_cast<float>(luaL_checknumber(state, 4));
  const char* ignoredEntityId = luaL_optstring(state, 5, "");
  const bool overlaps = host != nullptr && host->physicsOverlapBox(x, y, width, height, ignoredEntityId);
  lua_pushboolean(state, overlaps);
  return 1;
}

int hudTextBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* id = luaL_checkstring(state, 1);
  const char* text = luaL_checkstring(state, 2);
  const float x = static_cast<float>(luaL_checknumber(state, 3));
  const float y = static_cast<float>(luaL_checknumber(state, 4));
  const float scale = static_cast<float>(luaL_optnumber(state, 5, 3.0));
  const bool changed = host != nullptr && host->createHudText(id, text, x, y, scale);
  lua_pushboolean(state, changed);
  return 1;
}

int hudSetTextBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* id = luaL_checkstring(state, 1);
  const char* text = luaL_checkstring(state, 2);
  const bool changed = host != nullptr && host->setHudText(id, text);
  lua_pushboolean(state, changed);
  return 1;
}

int hudGetTextBinding(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  const char* id = luaL_checkstring(state, 1);
  const std::optional<std::string> text = host != nullptr ? host->hudText(id) : std::nullopt;
  if (!text.has_value()) {
    lua_pushnil(state);
    return 1;
  }
  lua_pushstring(state, text->c_str());
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

void configureLuaPackagePath(lua_State* state, const ProjectData& project) {
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }

  lua_getfield(state, -1, "path");
  const char* currentPath = lua_tostring(state, -1);
  const std::string current = currentPath != nullptr ? currentPath : "";
  lua_pop(state, 1);

  const std::filesystem::path scripts = project.projectDirectory / "scripts";
  const std::string projectRoot = project.projectDirectory.string();
  const std::string scriptsRoot = scripts.string();
  const std::string path = scriptsRoot + "/?.lua;" +
                           scriptsRoot + "/?/init.lua;" +
                           projectRoot + "/?.lua;" +
                           projectRoot + "/?/init.lua;" +
                           current;

  lua_pushstring(state, path.c_str());
  lua_setfield(state, -2, "path");
  lua_pop(state, 1);
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
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, debugLine, 1);
  lua_setfield(state, -2, "line");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, debugClearLines, 1);
  lua_setfield(state, -2, "clear_lines");
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
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, inputMouseDown, 1);
  lua_setfield(state, -2, "mouse_down");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, inputMousePosition, 1);
  lua_setfield(state, -2, "mouse_position");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, inputMouseWorldPosition, 1);
  lua_setfield(state, -2, "mouse_world_position");
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

  lua_newtable(state);
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, rigidbodyGetVelocity, 1);
  lua_setfield(state, -2, "get_velocity");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, rigidbodySetVelocity, 1);
  lua_setfield(state, -2, "set_velocity");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, rigidbodySetVelocityX, 1);
  lua_setfield(state, -2, "set_velocity_x");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, rigidbodySetVelocityY, 1);
  lua_setfield(state, -2, "set_velocity_y");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, rigidbodyAddImpulse, 1);
  lua_setfield(state, -2, "add_impulse");
  lua_setglobal(state, "Rigidbody2D");

  lua_newtable(state);
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, physicsOverlapBoxBinding, 1);
  lua_setfield(state, -2, "overlap_box");
  lua_setglobal(state, "Physics2D");

  lua_newtable(state);
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, hudTextBinding, 1);
  lua_setfield(state, -2, "text");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, hudSetTextBinding, 1);
  lua_setfield(state, -2, "set_text");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, hudGetTextBinding, 1);
  lua_setfield(state, -2, "get_text");
  lua_setglobal(state, "Hud");

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

  configureLuaPackagePath(state, project);

  if (!project.scriptEntry.empty()) {
    const std::filesystem::path scriptPath = resolveScriptPath(project, project.scriptEntry);
    if (luaL_dofile(state, scriptPath.string().c_str()) != LUA_OK) {
      error = "Failed to load Lua project script " + scriptPath.string() + ": " + lua_tostring(state, -1);
      lua_pop(state, 1);
      return false;
    }

    if (!lua_istable(state, -1)) {
      lua_pop(state, 1);
      error = "Lua project script must return a table: " + scriptPath.string();
      return false;
    }

    const int tableRef = luaL_ref(state, LUA_REGISTRYINDEX);
    scripts_.push_back(ScriptInstance{.entityId = "", .module = project.scriptEntry, .tableRef = tableRef});
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
    lua_pushnumber(state, entity.luaScript->jumpSpeed);
    lua_setfield(state, -2, "jump_speed");

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

std::optional<Vec2> LuaScriptHost::getRigidbodyVelocity(const std::string& entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  return rigidbodyVelocity(*world_, entityId);
}

bool LuaScriptHost::setRigidbodyVelocity(const std::string& entityId, const float x, const float y) {
  return world_ != nullptr && demi::runtime::setRigidbodyVelocity(*world_, entityId, Vec2{.x = x, .y = y});
}

bool LuaScriptHost::setRigidbodyVelocityX(const std::string& entityId, const float x) {
  return world_ != nullptr && demi::runtime::setRigidbodyVelocityX(*world_, entityId, x);
}

bool LuaScriptHost::setRigidbodyVelocityY(const std::string& entityId, const float y) {
  return world_ != nullptr && demi::runtime::setRigidbodyVelocityY(*world_, entityId, y);
}

bool LuaScriptHost::addRigidbodyImpulse(const std::string& entityId, const float x, const float y) {
  return world_ != nullptr && demi::runtime::addRigidbodyImpulse(*world_, entityId, Vec2{.x = x, .y = y});
}

bool LuaScriptHost::physicsOverlapBox(const float x, const float y, const float width, const float height, const std::string& ignoredEntityId) const {
  return world_ != nullptr && overlapBox(*world_, Vec2{.x = x, .y = y}, Vec2{.x = width, .y = height}, ignoredEntityId);
}

bool LuaScriptHost::setHudText(const std::string& id, const std::string& text) {
  if (world_ == nullptr) {
    return false;
  }

  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.text = text;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::createHudText(const std::string& id, const std::string& text, const float x, const float y, const float scale) {
  if (world_ == nullptr) {
    return false;
  }

  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.text = text;
      element.position = Vec2{.x = x, .y = y};
      element.scale = scale;
      element.visible = true;
      return true;
    }
  }

  world_->hudText.push_back(HudTextElement{
    .id = id,
    .text = text,
    .position = Vec2{.x = x, .y = y},
    .scale = scale,
  });
  return true;
}

std::optional<std::string> LuaScriptHost::hudText(const std::string& id) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }

  for (const HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      return element.text;
    }
  }
  return std::nullopt;
}

bool LuaScriptHost::isMouseDown(const std::string& button) const {
  return input_ != nullptr && input_->mouseButtonsDown.contains(normalizedKey(button));
}

Vec2 LuaScriptHost::mousePosition() const {
  return input_ != nullptr ? input_->mousePosition : Vec2{};
}

Vec2 LuaScriptHost::mouseWorldPosition() const {
  if (input_ == nullptr || world_ == nullptr) {
    return {};
  }

  const Camera2DComponent* camera = activeCamera(*world_);
  const float orthographicSize = camera != nullptr ? camera->orthographicSize : 10.0F;
  const float pixelsPerUnit = static_cast<float>(viewportHeight_) / std::max(orthographicSize * 2.0F, 1.0F);
  return Vec2{
    .x = (input_->mousePosition.x - static_cast<float>(viewportWidth_) * 0.5F) / pixelsPerUnit,
    .y = (static_cast<float>(viewportHeight_) * 0.5F - input_->mousePosition.y) / pixelsPerUnit,
  };
}

void LuaScriptHost::addDebugLine(const float x1, const float y1, const float x2, const float y2, const float r, const float g, const float b, const float a) {
  if (world_ == nullptr) {
    return;
  }
  world_->debugLines.push_back(DebugLine{
    .start = Vec2{.x = x1, .y = y1},
    .end = Vec2{.x = x2, .y = y2},
    .color = Color{.r = r, .g = g, .b = b, .a = a},
  });
}

void LuaScriptHost::clearDebugLines() {
  if (world_ != nullptr) {
    world_->debugLines.clear();
  }
}

void LuaScriptHost::setViewport(const int width, const int height) {
  viewportWidth_ = std::max(width, 1);
  viewportHeight_ = std::max(height, 1);
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
