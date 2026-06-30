#include "demi/runtime/LuaScriptHost.h"

#include "demi/runtime/AudioSystem.h"
#include "demi/runtime/Physics2D.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

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

std::string sanitizedSaveSlot(std::string slot) {
  if (slot.empty()) {
    return "settings";
  }
  for (char& c : slot) {
    const unsigned char value = static_cast<unsigned char>(c);
    if (!std::isalnum(value) && c != '_' && c != '-') {
      c = '_';
    }
  }
  return slot;
}

std::string escapeJsonString(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const char c : text) {
    if (c == '\\' || c == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(c);
  }
  return escaped;
}

std::optional<std::size_t> matchingClose(const std::string& text, const std::size_t open, const char openChar, const char closeChar) {
  int depth = 0;
  bool inString = false;
  bool escaped = false;
  for (std::size_t i = open; i < text.size(); ++i) {
    const char c = text[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c == '\\' && inString) {
      escaped = true;
      continue;
    }
    if (c == '"') {
      inString = !inString;
      continue;
    }
    if (inString) {
      continue;
    }
    if (c == openChar) {
      ++depth;
    } else if (c == closeChar) {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }
  return std::nullopt;
}

std::string unescapeJsonString(const std::string& text) {
  std::string unescaped;
  unescaped.reserve(text.size());
  bool escaped = false;
  for (const char c : text) {
    if (escaped) {
      unescaped.push_back(c);
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    unescaped.push_back(c);
  }
  return unescaped;
}

std::unordered_map<std::string, LuaScriptHost::SaveValue> parseSaveState(const std::string& text) {
  std::unordered_map<std::string, LuaScriptHost::SaveValue> values;
  const std::size_t stateKey = text.find("\"state\"");
  const std::size_t open = stateKey == std::string::npos ? std::string::npos : text.find('{', stateKey);
  if (open == std::string::npos) {
    return values;
  }
  const std::optional<std::size_t> close = matchingClose(text, open, '{', '}');
  if (!close.has_value()) {
    return values;
  }

  std::size_t cursor = open + 1;
  while (cursor < *close) {
    const std::size_t keyStart = text.find('"', cursor);
    if (keyStart == std::string::npos || keyStart >= *close) {
      break;
    }
    const std::size_t keyEnd = text.find('"', keyStart + 1);
    if (keyEnd == std::string::npos || keyEnd >= *close) {
      break;
    }
    const std::size_t colon = text.find(':', keyEnd + 1);
    if (colon == std::string::npos || colon >= *close) {
      break;
    }

    std::size_t valueStart = text.find_first_not_of(" \t\r\n", colon + 1);
    if (valueStart == std::string::npos || valueStart >= *close) {
      break;
    }

    const std::string key = unescapeJsonString(text.substr(keyStart + 1, keyEnd - keyStart - 1));
    if (text[valueStart] == '"') {
      const std::size_t valueEnd = text.find('"', valueStart + 1);
      if (valueEnd == std::string::npos || valueEnd >= *close) {
        break;
      }
      values[key] = LuaScriptHost::SaveValue{.value = unescapeJsonString(text.substr(valueStart + 1, valueEnd - valueStart - 1)), .number = false};
      cursor = valueEnd + 1;
    } else {
      const std::size_t valueEnd = text.find_first_of(",}\r\n", valueStart);
      const std::size_t end = valueEnd == std::string::npos ? *close : std::min(valueEnd, *close);
      std::string value = text.substr(valueStart, end - valueStart);
      while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
      }
      values[key] = LuaScriptHost::SaveValue{.value = value, .number = true};
      cursor = end + 1;
    }
  }
  return values;
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

int inputViewportSize(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  const Vec2 size = host != nullptr ? host->viewportSize() : Vec2{.x = 1.0F, .y = 1.0F};
  lua_pushnumber(state, size.x);
  lua_pushnumber(state, size.y);
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

std::string stringField(lua_State* state, const int tableIndex, const char* fieldName, const std::string& fallback = {}) {
  const int index = lua_absindex(state, tableIndex);
  lua_getfield(state, index, fieldName);
  std::string value = fallback;
  if (lua_isstring(state, -1)) {
    value = lua_tostring(state, -1);
  }
  lua_pop(state, 1);
  return value;
}

float numberField(lua_State* state, const int tableIndex, const char* fieldName, const float fallback = 0.0F) {
  const int index = lua_absindex(state, tableIndex);
  lua_getfield(state, index, fieldName);
  const float value = lua_isnumber(state, -1) ? static_cast<float>(lua_tonumber(state, -1)) : fallback;
  lua_pop(state, 1);
  return value;
}

bool boolField(lua_State* state, const int tableIndex, const char* fieldName, const bool fallback = false) {
  const int index = lua_absindex(state, tableIndex);
  lua_getfield(state, index, fieldName);
  const bool value = lua_isboolean(state, -1) ? lua_toboolean(state, -1) != 0 : fallback;
  lua_pop(state, 1);
  return value;
}

Vec2 vec2Field(lua_State* state, const int tableIndex, const char* fieldName, const Vec2 fallback = {}) {
  const int index = lua_absindex(state, tableIndex);
  lua_getfield(state, index, fieldName);
  Vec2 value = fallback;
  if (lua_istable(state, -1)) {
    lua_rawgeti(state, -1, 1);
    if (lua_isnumber(state, -1)) {
      value.x = static_cast<float>(lua_tonumber(state, -1));
    }
    lua_pop(state, 1);

    lua_rawgeti(state, -1, 2);
    if (lua_isnumber(state, -1)) {
      value.y = static_cast<float>(lua_tonumber(state, -1));
    }
    lua_pop(state, 1);
  }
  lua_pop(state, 1);
  return value;
}

void parseTransform2D(lua_State* state, const int componentIndex, Entity& entity) {
  Transform2DComponent component;
  component.position = vec2Field(state, componentIndex, "position", component.position);
  component.rotation = numberField(state, componentIndex, "rotation", component.rotation);
  component.scale = vec2Field(state, componentIndex, "scale", component.scale);
  entity.transform2D = component;
}

void parseRigidbody2D(lua_State* state, const int componentIndex, Entity& entity) {
  Rigidbody2DComponent component;
  component.bodyType = stringField(state, componentIndex, "body_type", component.bodyType);
  component.velocity = vec2Field(state, componentIndex, "velocity", component.velocity);
  component.gravityScale = numberField(state, componentIndex, "gravity_scale", component.gravityScale);
  component.bounciness = numberField(state, componentIndex, "bounciness", component.bounciness);
  component.lockRotation = boolField(state, componentIndex, "lock_rotation", component.lockRotation);
  entity.rigidbody2D = component;
}

void parseBoxCollider2D(lua_State* state, const int componentIndex, Entity& entity) {
  BoxCollider2DComponent component;
  component.size = vec2Field(state, componentIndex, "size", component.size);
  component.offset = vec2Field(state, componentIndex, "offset", component.offset);
  component.isTrigger = boolField(state, componentIndex, "is_trigger", component.isTrigger);
  entity.boxCollider2D = component;
}

void parseSprite(lua_State* state, const int componentIndex, Entity& entity) {
  SpriteComponent component;
  component.texture = stringField(state, componentIndex, "texture", component.texture);
  component.layer = stringField(state, componentIndex, "layer", component.layer);
  entity.sprite = component;
}

void parseLuaComponent(lua_State* state, const int componentsIndex, const char* componentName, Entity& entity) {
  lua_getfield(state, componentsIndex, componentName);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }

  const int componentIndex = lua_absindex(state, -1);
  if (std::string_view(componentName) == "Transform2D") {
    parseTransform2D(state, componentIndex, entity);
  } else if (std::string_view(componentName) == "Rigidbody2D") {
    parseRigidbody2D(state, componentIndex, entity);
  } else if (std::string_view(componentName) == "BoxCollider2D") {
    parseBoxCollider2D(state, componentIndex, entity);
  } else if (std::string_view(componentName) == "Sprite") {
    parseSprite(state, componentIndex, entity);
  }
  lua_pop(state, 1);
}

int entityCreateBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* entityId = luaL_checkstring(state, 1);
  luaL_checktype(state, 2, LUA_TTABLE);

  Entity entity;
  entity.id = entityId;
  entity.name = stringField(state, 2, "name", entity.id);

  lua_getfield(state, 2, "components");
  const int componentsIndex = lua_istable(state, -1) ? lua_absindex(state, -1) : lua_absindex(state, 2);
  parseLuaComponent(state, componentsIndex, "Transform2D", entity);
  parseLuaComponent(state, componentsIndex, "Rigidbody2D", entity);
  parseLuaComponent(state, componentsIndex, "BoxCollider2D", entity);
  parseLuaComponent(state, componentsIndex, "Sprite", entity);
  lua_pop(state, 1);

  const bool created = host != nullptr && host->createEntity(std::move(entity));
  lua_pushboolean(state, created);
  return 1;
}

int entityDestroyBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* entityId = luaL_checkstring(state, 1);
  const bool destroyed = host != nullptr && host->destroyEntity(entityId);
  lua_pushboolean(state, destroyed);
  return 1;
}

int sceneLoad(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* sceneId = luaL_checkstring(state, 1);
  if (host != nullptr) {
    host->requestSceneLoad(sceneId);
    lua_pushboolean(state, true);
  } else {
    std::cerr << "Scene.load is not available without a runtime host.\n";
    lua_pushboolean(state, false);
  }
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

Color colorArgs(lua_State* state, const int startIndex, const Color fallback = {}) {
  return Color{
    .r = static_cast<float>(luaL_optnumber(state, startIndex, fallback.r)),
    .g = static_cast<float>(luaL_optnumber(state, startIndex + 1, fallback.g)),
    .b = static_cast<float>(luaL_optnumber(state, startIndex + 2, fallback.b)),
    .a = static_cast<float>(luaL_optnumber(state, startIndex + 3, fallback.a)),
  };
}

int hudTextBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* id = luaL_checkstring(state, 1);
  const char* text = luaL_checkstring(state, 2);
  const float x = static_cast<float>(luaL_checknumber(state, 3));
  const float y = static_cast<float>(luaL_checknumber(state, 4));
  const float scale = static_cast<float>(luaL_optnumber(state, 5, 3.0));
  const Color color = colorArgs(state, 6, Color{.r = 1.0F, .g = 1.0F, .b = 1.0F, .a = 1.0F});
  const bool changed = host != nullptr && host->createHudText(id, text, x, y, scale, color);
  lua_pushboolean(state, changed);
  return 1;
}

int hudRectBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* id = luaL_checkstring(state, 1);
  const float x = static_cast<float>(luaL_checknumber(state, 2));
  const float y = static_cast<float>(luaL_checknumber(state, 3));
  const float width = static_cast<float>(luaL_checknumber(state, 4));
  const float height = static_cast<float>(luaL_checknumber(state, 5));
  const Color color = colorArgs(state, 6, Color{.r = 1.0F, .g = 1.0F, .b = 1.0F, .a = 1.0F});
  const bool changed = host != nullptr && host->createHudRect(id, x, y, width, height, color);
  lua_pushboolean(state, changed);
  return 1;
}

int hudSetRectBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* id = luaL_checkstring(state, 1);
  const float x = static_cast<float>(luaL_checknumber(state, 2));
  const float y = static_cast<float>(luaL_checknumber(state, 3));
  const float width = static_cast<float>(luaL_checknumber(state, 4));
  const float height = static_cast<float>(luaL_checknumber(state, 5));
  const bool changed = host != nullptr && host->setHudRect(id, x, y, width, height);
  lua_pushboolean(state, changed);
  return 1;
}

int hudSetColorBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* id = luaL_checkstring(state, 1);
  const Color color = colorArgs(state, 2, Color{.r = 1.0F, .g = 1.0F, .b = 1.0F, .a = 1.0F});
  const bool changed = host != nullptr && host->setHudColor(id, color);
  lua_pushboolean(state, changed);
  return 1;
}

int hudSetVisibleBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* id = luaL_checkstring(state, 1);
  const bool visible = lua_toboolean(state, 2) != 0;
  const bool changed = host != nullptr && host->setHudVisible(id, visible);
  lua_pushboolean(state, changed);
  return 1;
}

int hudSetGroupVisibleBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* group = luaL_checkstring(state, 1);
  const bool visible = lua_toboolean(state, 2) != 0;
  const bool changed = host != nullptr && host->setHudGroupVisible(group, visible);
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

int saveGetNumberBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* slot = luaL_checkstring(state, 1);
  const char* key = luaL_checkstring(state, 2);
  const float fallback = static_cast<float>(luaL_optnumber(state, 3, 0.0));
  const std::optional<float> value = host != nullptr ? host->saveNumber(slot, key) : std::nullopt;
  lua_pushnumber(state, value.value_or(fallback));
  return 1;
}

int saveSetNumberBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* slot = luaL_checkstring(state, 1);
  const char* key = luaL_checkstring(state, 2);
  const float value = static_cast<float>(luaL_checknumber(state, 3));
  const bool changed = host != nullptr && host->setSaveNumber(slot, key, value);
  lua_pushboolean(state, changed);
  return 1;
}

int saveGetStringBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* slot = luaL_checkstring(state, 1);
  const char* key = luaL_checkstring(state, 2);
  const char* fallback = luaL_optstring(state, 3, "");
  const std::optional<std::string> value = host != nullptr ? host->saveString(slot, key) : std::nullopt;
  lua_pushstring(state, value.value_or(fallback).c_str());
  return 1;
}

int saveSetStringBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* slot = luaL_checkstring(state, 1);
  const char* key = luaL_checkstring(state, 2);
  const char* value = luaL_checkstring(state, 3);
  const bool changed = host != nullptr && host->setSaveString(slot, key, value);
  lua_pushboolean(state, changed);
  return 1;
}

int audioPlayBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* assetId = luaL_checkstring(state, 1);
  const std::uint64_t handle = host != nullptr ? host->playAudio(assetId) : 0;
  lua_pushinteger(state, static_cast<lua_Integer>(handle));
  return 1;
}

int audioStopBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const std::uint64_t handle = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
  const bool stopped = host != nullptr && host->stopAudio(handle);
  lua_pushboolean(state, stopped);
  return 1;
}

int audioSetMasterVolumeBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const float volume = static_cast<float>(luaL_checknumber(state, 1));
  if (host != nullptr) {
    host->setMasterVolume(volume);
  }
  return 0;
}

int audioGetMasterVolumeBinding(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  lua_pushnumber(state, host != nullptr ? host->masterVolume() : 1.0F);
  return 1;
}

int runtimeQuitBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  if (host != nullptr) {
    host->requestQuit();
  }
  return 0;
}

int runtimeSetPhysicsEnabledBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const bool enabled = lua_toboolean(state, 1) != 0;
  if (host != nullptr) {
    host->setPhysicsEnabled(enabled);
  }
  return 0;
}

int runtimeSetWindowModeBinding(lua_State* state) {
  LuaScriptHost* host = hostFromUpvalue(state);
  const char* mode = luaL_checkstring(state, 1);
  if (host != nullptr) {
    host->setWindowMode(mode);
  }
  return 0;
}

int runtimeGetWindowModeBinding(lua_State* state) {
  const LuaScriptHost* host = hostFromUpvalue(state);
  lua_pushstring(state, host != nullptr ? host->windowMode().c_str() : "windowed");
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

void callUiEvent(lua_State* state, const int tableRef, const char* functionName, const HudButtonElement& button, const Vec2 mousePosition) {
  lua_rawgeti(state, LUA_REGISTRYINDEX, tableRef);
  lua_getfield(state, -1, functionName);
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }

  lua_pushvalue(state, -2);
  lua_newtable(state);
  lua_pushstring(state, button.id.c_str());
  lua_setfield(state, -2, "id");
  lua_pushstring(state, button.label.c_str());
  lua_setfield(state, -2, "label");
  lua_pushnumber(state, mousePosition.x);
  lua_setfield(state, -2, "mouse_x");
  lua_pushnumber(state, mousePosition.y);
  lua_setfield(state, -2, "mouse_y");

  if (lua_pcall(state, 2, 0, 0) != LUA_OK) {
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
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, inputViewportSize, 1);
  lua_setfield(state, -2, "viewport_size");
  lua_setglobal(state, "Input");

  lua_newtable(state);
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, entityFind, 1);
  lua_setfield(state, -2, "find");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, entityCreateBinding, 1);
  lua_setfield(state, -2, "create");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, entityDestroyBinding, 1);
  lua_setfield(state, -2, "destroy");
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
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, sceneLoad, 1);
  lua_setfield(state, -2, "load");
  lua_setglobal(state, "Scene");

  lua_newtable(state);
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, runtimeQuitBinding, 1);
  lua_setfield(state, -2, "quit");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, runtimeSetPhysicsEnabledBinding, 1);
  lua_setfield(state, -2, "set_physics_enabled");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, runtimeSetWindowModeBinding, 1);
  lua_setfield(state, -2, "set_window_mode");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, runtimeGetWindowModeBinding, 1);
  lua_setfield(state, -2, "get_window_mode");
  lua_setglobal(state, "Runtime");

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
  lua_pushcclosure(state, hudRectBinding, 1);
  lua_setfield(state, -2, "rect");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, hudSetTextBinding, 1);
  lua_setfield(state, -2, "set_text");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, hudSetRectBinding, 1);
  lua_setfield(state, -2, "set_rect");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, hudSetColorBinding, 1);
  lua_setfield(state, -2, "set_color");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, hudSetVisibleBinding, 1);
  lua_setfield(state, -2, "set_visible");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, hudSetGroupVisibleBinding, 1);
  lua_setfield(state, -2, "set_group_visible");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, hudGetTextBinding, 1);
  lua_setfield(state, -2, "get_text");
  lua_setglobal(state, "Hud");

  lua_newtable(state);
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, saveGetNumberBinding, 1);
  lua_setfield(state, -2, "get_number");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, saveSetNumberBinding, 1);
  lua_setfield(state, -2, "set_number");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, saveGetStringBinding, 1);
  lua_setfield(state, -2, "get_string");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, saveSetStringBinding, 1);
  lua_setfield(state, -2, "set_string");
  lua_setglobal(state, "Save");

  lua_newtable(state);
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, audioPlayBinding, 1);
  lua_setfield(state, -2, "play");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, audioStopBinding, 1);
  lua_setfield(state, -2, "stop");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, audioSetMasterVolumeBinding, 1);
  lua_setfield(state, -2, "set_master_volume");
  lua_pushlightuserdata(state, this);
  lua_pushcclosure(state, audioGetMasterVolumeBinding, 1);
  lua_setfield(state, -2, "get_master_volume");
  lua_setglobal(state, "Audio");

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

  projectDirectory_ = project.projectDirectory;
  project_ = &project;
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

  for (const HudButtonElement& button : world.hudButtons) {
    if (button.script.empty()) {
      continue;
    }

    const std::filesystem::path scriptPath = resolveScriptPath(project, button.script);
    if (luaL_dofile(state, scriptPath.string().c_str()) != LUA_OK) {
      error = "Failed to load HUD button Lua script " + scriptPath.string() + ": " + lua_tostring(state, -1);
      lua_pop(state, 1);
      return false;
    }

    if (!lua_istable(state, -1)) {
      lua_pop(state, 1);
      error = "HUD button Lua script must return a table: " + scriptPath.string();
      return false;
    }

    lua_pushstring(state, button.id.c_str());
    lua_setfield(state, -2, "entity_id");
    lua_pushstring(state, button.id.c_str());
    lua_setfield(state, -2, "ui_id");

    const int tableRef = luaL_ref(state, LUA_REGISTRYINDEX);
    scripts_.push_back(ScriptInstance{.entityId = button.id, .module = button.script, .tableRef = tableRef});
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

bool LuaScriptHost::destroyEntity(const std::string& entityId) {
  if (world_ == nullptr) {
    return false;
  }

  const auto before = world_->entities.size();
  std::erase_if(world_->entities, [&](const Entity& entity) { return entity.id == entityId; });
  return world_->entities.size() != before;
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

bool LuaScriptHost::createEntity(Entity entity) {
  if (world_ == nullptr || entity.id.empty()) {
    return false;
  }

  if (entity.name.empty()) {
    entity.name = entity.id;
  }

  if (Entity* existing = findEntity(*world_, entity.id)) {
    *existing = std::move(entity);
    return true;
  }

  world_->entities.push_back(std::move(entity));
  return true;
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

bool LuaScriptHost::createHudText(const std::string& id, const std::string& text, const float x, const float y, const float scale, const Color color) {
  if (world_ == nullptr) {
    return false;
  }

  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.text = text;
      element.position = Vec2{.x = x, .y = y};
      element.scale = scale;
      element.color = color;
      element.visible = true;
      return true;
    }
  }

  world_->hudText.push_back(HudTextElement{
    .id = id,
    .group = {},
    .text = text,
    .position = Vec2{.x = x, .y = y},
    .scale = scale,
    .color = color,
  });
  return true;
}

bool LuaScriptHost::createHudRect(const std::string& id, const float x, const float y, const float width, const float height, const Color color) {
  if (world_ == nullptr) {
    return false;
  }

  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.position = Vec2{.x = x, .y = y};
      element.size = Vec2{.x = width, .y = height};
      element.color = color;
      element.visible = true;
      return true;
    }
  }

  world_->hudRects.push_back(HudRectElement{
    .id = id,
    .group = {},
    .position = Vec2{.x = x, .y = y},
    .size = Vec2{.x = width, .y = height},
    .color = color,
  });
  return true;
}

bool LuaScriptHost::setHudRect(const std::string& id, const float x, const float y, const float width, const float height) {
  if (world_ == nullptr) {
    return false;
  }

  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.position = Vec2{.x = x, .y = y};
      element.size = Vec2{.x = width, .y = height};
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudColor(const std::string& id, const Color color) {
  if (world_ == nullptr) {
    return false;
  }

  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.color = color;
      return true;
    }
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.id == id) {
      element.color = color;
      return true;
    }
  }
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.color = color;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudVisible(const std::string& id, const bool visible) {
  if (world_ == nullptr) {
    return false;
  }

  bool changed = false;
  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.id == id) {
      element.visible = visible;
      if (!visible) {
        element.hovered = false;
      }
      changed = true;
    }
  }
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.visible = visible;
      changed = true;
    }
  }
  return changed;
}

bool LuaScriptHost::setHudGroupVisible(const std::string& group, const bool visible) {
  if (world_ == nullptr || group.empty()) {
    return false;
  }

  bool changed = false;
  for (HudRectElement& element : world_->hudRects) {
    if (element.group == group) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.group == group) {
      element.visible = visible;
      if (!visible) {
        element.hovered = false;
      }
      changed = true;
    }
  }
  for (HudTextElement& element : world_->hudText) {
    if (element.group == group) {
      element.visible = visible;
      changed = true;
    }
  }
  return changed;
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

std::unordered_map<std::string, LuaScriptHost::SaveValue>& LuaScriptHost::loadSaveSlot(const std::string& slot) {
  const std::string safeSlot = sanitizedSaveSlot(slot);
  if (const auto found = saves_.find(safeSlot); found != saves_.end()) {
    return found->second;
  }

  std::unordered_map<std::string, SaveValue> values;
  const std::filesystem::path path = projectDirectory_ / "saves" / (safeSlot + ".save.json");
  std::ifstream input(path);
  if (input) {
    std::ostringstream buffer;
    buffer << input.rdbuf();
    values = parseSaveState(buffer.str());
  }

  auto [inserted, _] = saves_.emplace(safeSlot, std::move(values));
  return inserted->second;
}

bool LuaScriptHost::writeSaveSlot(const std::string& slot) {
  if (projectDirectory_.empty()) {
    return false;
  }

  const std::string safeSlot = sanitizedSaveSlot(slot);
  auto found = saves_.find(safeSlot);
  if (found == saves_.end()) {
    return false;
  }

  const std::filesystem::path savesDirectory = projectDirectory_ / "saves";
  std::error_code error;
  std::filesystem::create_directories(savesDirectory, error);
  if (error) {
    return false;
  }

  std::ofstream output(savesDirectory / (safeSlot + ".save.json"));
  if (!output) {
    return false;
  }

  output << "{\n";
  output << "  \"format_version\": 1,\n";
  output << "  \"slot\": \"" << escapeJsonString(safeSlot) << "\",\n";
  output << "  \"state\": {\n";

  std::vector<std::string> keys;
  keys.reserve(found->second.size());
  for (const auto& [key, _] : found->second) {
    keys.push_back(key);
  }
  std::ranges::sort(keys);

  for (std::size_t i = 0; i < keys.size(); ++i) {
    const SaveValue& value = found->second.at(keys[i]);
    output << "    \"" << escapeJsonString(keys[i]) << "\": ";
    if (value.number) {
      output << value.value;
    } else {
      output << "\"" << escapeJsonString(value.value) << "\"";
    }
    output << (i + 1 < keys.size() ? ",\n" : "\n");
  }

  output << "  }\n";
  output << "}\n";
  return true;
}

std::optional<float> LuaScriptHost::saveNumber(const std::string& slot, const std::string& key) {
  std::unordered_map<std::string, SaveValue>& values = loadSaveSlot(slot);
  const auto found = values.find(key);
  if (found == values.end() || !found->second.number) {
    return std::nullopt;
  }

  try {
    return std::stof(found->second.value);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> LuaScriptHost::saveString(const std::string& slot, const std::string& key) {
  std::unordered_map<std::string, SaveValue>& values = loadSaveSlot(slot);
  const auto found = values.find(key);
  if (found == values.end() || found->second.number) {
    return std::nullopt;
  }
  return found->second.value;
}

bool LuaScriptHost::setSaveNumber(const std::string& slot, const std::string& key, const float value) {
  std::unordered_map<std::string, SaveValue>& values = loadSaveSlot(slot);
  std::ostringstream stream;
  stream << std::setprecision(6) << value;
  values[key] = SaveValue{.value = stream.str(), .number = true};
  return writeSaveSlot(slot);
}

bool LuaScriptHost::setSaveString(const std::string& slot, const std::string& key, const std::string& value) {
  std::unordered_map<std::string, SaveValue>& values = loadSaveSlot(slot);
  values[key] = SaveValue{.value = value, .number = false};
  return writeSaveSlot(slot);
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
  const Vec2 cameraPosition = activeCameraPosition(*world_);
  const float orthographicSize = camera != nullptr ? camera->orthographicSize : 10.0F;
  const float pixelsPerUnit = static_cast<float>(viewportHeight_) / std::max(orthographicSize * 2.0F, 1.0F);
  return Vec2{
    .x = cameraPosition.x + (input_->mousePosition.x - static_cast<float>(viewportWidth_) * 0.5F) / pixelsPerUnit,
    .y = cameraPosition.y + (static_cast<float>(viewportHeight_) * 0.5F - input_->mousePosition.y) / pixelsPerUnit,
  };
}

Vec2 LuaScriptHost::viewportSize() const {
  return Vec2{.x = static_cast<float>(viewportWidth_), .y = static_cast<float>(viewportHeight_)};
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

std::uint64_t LuaScriptHost::playAudio(const std::string& assetId) {
  return audio_ != nullptr ? audio_->play(assetId) : 0;
}

bool LuaScriptHost::stopAudio(const std::uint64_t handle) {
  return audio_ != nullptr && audio_->stop(handle);
}

void LuaScriptHost::setMasterVolume(const float volume) {
  if (audio_ != nullptr) {
    audio_->setMasterVolume(volume);
  }
}

float LuaScriptHost::masterVolume() const {
  return audio_ != nullptr ? audio_->masterVolume() : 1.0F;
}

void LuaScriptHost::setViewport(const int width, const int height) {
  viewportWidth_ = std::max(width, 1);
  viewportHeight_ = std::max(height, 1);
}

void LuaScriptHost::dispatchHudEvents() {
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr || world_ == nullptr || input_ == nullptr) {
    return;
  }

  const Vec2 mouse{
    .x = input_->mousePosition.x * std::max(world_->hudCanvasSize.x, 1.0F) / static_cast<float>(std::max(viewportWidth_, 1)),
    .y = input_->mousePosition.y * std::max(world_->hudCanvasSize.y, 1.0F) / static_cast<float>(std::max(viewportHeight_, 1)),
  };
  const bool mouseDown = isMouseDown("left");
  for (HudButtonElement& button : world_->hudButtons) {
    if (!button.visible) {
      button.hovered = false;
      continue;
    }

    button.hovered = mouse.x >= button.position.x &&
                     mouse.x <= button.position.x + button.size.x &&
                     mouse.y >= button.position.y &&
                     mouse.y <= button.position.y + button.size.y;
    if (!button.hovered) {
      continue;
    }

    for (const ScriptInstance& script : scripts_) {
      if (script.entityId != button.id) {
        continue;
      }
      callUiEvent(state, script.tableRef, "on_ui_hover", button, mouse);
      if (mouseDown && !previousUiMouseDown_) {
        callUiEvent(state, script.tableRef, "on_ui_click", button, mouse);
      }
    }
  }

  previousUiMouseDown_ = mouseDown;
#endif
}

void LuaScriptHost::requestQuit() {
  quitRequested_ = true;
}

bool LuaScriptHost::quitRequested() const {
  return quitRequested_;
}

void LuaScriptHost::setWindowMode(std::string mode) {
  mode = normalizedKey(std::move(mode));
  if (mode != "windowed" && mode != "borderless" && mode != "fullscreen") {
    return;
  }
  if (windowMode_ == mode) {
    return;
  }
  windowMode_ = std::move(mode);
  windowModeDirty_ = true;
}

const std::string& LuaScriptHost::windowMode() const {
  return windowMode_;
}

bool LuaScriptHost::windowModeDirty() const {
  return windowModeDirty_;
}

void LuaScriptHost::clearWindowModeDirty() {
  windowModeDirty_ = false;
}

void LuaScriptHost::setPhysicsEnabled(const bool enabled) {
  physicsEnabled_ = enabled;
}

bool LuaScriptHost::physicsEnabled() const {
  return physicsEnabled_;
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

  dispatchHudEvents();

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
  unloadScripts();
}

void LuaScriptHost::unloadScripts() {
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr) {
    scripts_.clear();
    return;
  }
  for (const ScriptInstance& script : scripts_) {
    callLifecycle(state, script.tableRef, "on_destroy");
    luaL_unref(state, LUA_REGISTRYINDEX, script.tableRef);
  }
  scripts_.clear();
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
