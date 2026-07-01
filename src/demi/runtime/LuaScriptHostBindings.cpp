#include "demi/runtime/LuaScriptHostInternal.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <iostream>
#include <optional>
#include <string_view>
#include <system_error>
#include <tuple>

#include <nlohmann/json.hpp>

namespace demi::runtime {

#if DEMI_HAS_LUA54
namespace {

int luaTraceback(lua_State* state) {
  const char* message = lua_tostring(state, 1);
  if (message != nullptr) {
    luaL_traceback(state, state, message, 1);
  } else {
    lua_pushliteral(state, "Lua error with non-string value");
  }
  return 1;
}

std::tuple<sol::object, sol::object> vec2Result(lua_State* state, const std::optional<Vec2> value) {
  if (!value.has_value()) {
    return {sol::nil, sol::nil};
  }
  return {sol::make_object(state, value->x), sol::make_object(state, value->y)};
}

Vec2 vec2Field(const sol::table table, const char* fieldName, const Vec2 fallback = {}) {
  const sol::object object = table[fieldName];
  if (!object.is<sol::table>()) {
    return fallback;
  }
  const sol::table vec = object.as<sol::table>();
  return Vec2{.x = vec.get_or(1, fallback.x), .y = vec.get_or(2, fallback.y)};
}

sol::table componentTable(const sol::table components, const char* name) {
  const sol::object object = components[name];
  return object.is<sol::table>() ? object.as<sol::table>() : sol::table{};
}

Entity parseEntitySpec(const std::string& entityId, const sol::table spec) {
  Entity entity;
  entity.id = entityId;
  entity.name = spec.get_or("name", entity.id);

  const sol::object componentsObject = spec["components"];
  const sol::table components = componentsObject.is<sol::table>() ? componentsObject.as<sol::table>() : spec;

  if (const sol::table transform = componentTable(components, "Transform2D"); transform.valid()) {
    entity.transform2D = Transform2DComponent{
      .position = vec2Field(transform, "position"),
      .rotation = transform.get_or("rotation", 0.0F),
      .scale = vec2Field(transform, "scale", {1.0F, 1.0F}),
    };
  }

  if (const sol::table rigidbody = componentTable(components, "Rigidbody2D"); rigidbody.valid()) {
    entity.rigidbody2D = Rigidbody2DComponent{
      .bodyType = rigidbody.get_or("body_type", std::string("dynamic")),
      .velocity = vec2Field(rigidbody, "velocity"),
      .gravityScale = rigidbody.get_or("gravity_scale", 1.0F),
      .bounciness = rigidbody.get_or("bounciness", 0.0F),
      .lockRotation = rigidbody.get_or("lock_rotation", true),
    };
  }

  if (const sol::table collider = componentTable(components, "BoxCollider2D"); collider.valid()) {
    entity.boxCollider2D = BoxCollider2DComponent{
      .size = vec2Field(collider, "size", {1.0F, 1.0F}),
      .offset = vec2Field(collider, "offset"),
      .isTrigger = collider.get_or("is_trigger", false),
      .layer = collider.get_or("layer", std::string{}),
    };
  }

  if (const sol::table sprite = componentTable(components, "Sprite"); sprite.valid()) {
    entity.sprite = SpriteComponent{
      .texture = sprite.get_or("texture", std::string{}),
      .layer = sprite.get_or("layer", std::string{}),
    };
  }

  if (const sol::table audioSource = componentTable(components, "AudioSource"); audioSource.valid()) {
    entity.audioSource = AudioSourceComponent{
      .clip = audioSource.get_or("clip", std::string{}),
      .playOnStart = audioSource.get_or("play_on_start", false),
      .loop = audioSource.get_or("loop", false),
      .volume = audioSource.get_or("volume", 1.0F),
    };
  }

  if (const sol::table audioListener = componentTable(components, "AudioListener"); audioListener.valid()) {
    entity.audioListener = AudioListenerComponent{.primary = audioListener.get_or("primary", true)};
  }

  if (const sol::table videoPlayer = componentTable(components, "VideoPlayer"); videoPlayer.valid()) {
    entity.videoPlayer = VideoPlayerComponent{
      .clip = videoPlayer.get_or("clip", std::string{}),
      .playOnStart = videoPlayer.get_or("play_on_start", false),
      .loop = videoPlayer.get_or("loop", false),
    };
  }

  return entity;
}

int callbackRef(lua_State* state, const sol::function callback) {
  callback.push();
  return luaL_ref(state, LUA_REGISTRYINDEX);
}

std::uint64_t addTimer(lua_State* state, LuaScriptHost& host, const float seconds, const bool repeating, const sol::function callback) {
  const int ref = callbackRef(state, callback);
  const std::uint64_t id = host.addTimer(seconds, repeating, ref);
  if (id == 0) {
    luaL_unref(state, LUA_REGISTRYINDEX, ref);
  }
  return id;
}

std::uint64_t addEventSubscription(lua_State* state, LuaScriptHost& host, const std::string& eventName, const sol::function callback) {
  const int ref = callbackRef(state, callback);
  const std::uint64_t id = host.addEventSubscription(eventName, ref);
  if (id == 0) {
    luaL_unref(state, LUA_REGISTRYINDEX, ref);
  }
  return id;
}

int emitEvent(lua_State* state, LuaScriptHost& host, const std::string& eventName, const sol::object payload) {
  if (payload.valid() && payload != sol::nil) {
    payload.push();
  } else {
    lua_newtable(state);
  }
  const int payloadIndex = lua_gettop(state);
  const int delivered = host.emitEvent(eventName, payloadIndex);
  lua_pop(state, 1);
  return delivered;
}

nlohmann::json luaObjectToJson(const sol::object object) {
  switch (object.get_type()) {
  case sol::type::boolean:
    return object.as<bool>();
  case sol::type::number:
    return object.as<double>();
  case sol::type::string:
    return object.as<std::string>();
  case sol::type::table: {
    nlohmann::json value = nlohmann::json::object();
    const sol::table table = object.as<sol::table>();
    for (const auto& [key, item] : table) {
      if (key.is<std::string>()) {
        value[key.as<std::string>()] = luaObjectToJson(item);
      } else if (key.is<int>()) {
        value[std::to_string(key.as<int>())] = luaObjectToJson(item);
      }
    }
    return value;
  }
  default:
    return nullptr;
  }
}

sol::object jsonToLuaObject(lua_State* state, const nlohmann::json& value) {
  sol::state_view lua(state);
  if (value.is_object()) {
    sol::table table = lua.create_table();
    for (const auto& [key, item] : value.items()) {
      table.set(key, jsonToLuaObject(state, item));
    }
    return sol::make_object(state, table);
  }
  if (value.is_array()) {
    sol::table table = lua.create_table();
    int index = 1;
    for (const nlohmann::json& item : value) {
      table.set(index++, jsonToLuaObject(state, item));
    }
    return sol::make_object(state, table);
  }
  if (value.is_boolean()) {
    return sol::make_object(state, value.get<bool>());
  }
  if (value.is_number_integer()) {
    return sol::make_object(state, value.get<int>());
  }
  if (value.is_number()) {
    return sol::make_object(state, value.get<double>());
  }
  if (value.is_string()) {
    return sol::make_object(state, value.get<std::string>());
  }
  return sol::nil;
}

sol::object readSaveTable(lua_State* state, LuaScriptHost& host, const std::string& slot) {
  const std::optional<std::string> documentText = host.readSaveDocument(slot);
  if (!documentText.has_value()) {
    return sol::nil;
  }

  nlohmann::json document;
  try {
    document = nlohmann::json::parse(*documentText);
  } catch (...) {
    return sol::nil;
  }
  if (!document.is_object() || !document.contains("state") || !document["state"].is_object()) {
    return sol::nil;
  }

  int version = document.value("format_version", 1);
  sol::table stateTable = jsonToLuaObject(state, document["state"]).as<sol::table>();
  bool migrated = false;
  bool progressed = true;
  while (progressed) {
    progressed = false;
    for (const LuaScriptHost::SaveMigrationHook& hook : host.saveMigrationHooks()) {
      if (hook.fromVersion != version) {
        continue;
      }
      lua_rawgeti(state, LUA_REGISTRYINDEX, hook.callbackRef);
      stateTable.push();
      lua_pushinteger(state, hook.fromVersion);
      lua_pushinteger(state, hook.toVersion);
      std::string error;
      if (!luaCall(state, 3, 1, error)) {
        luaReportCallbackError("Save.register_migration", {}, slot, error);
        return sol::nil;
      }
      if (lua_istable(state, -1)) {
        stateTable = sol::stack::get<sol::table>(state, -1);
      }
      lua_pop(state, 1);
      version = hook.toVersion;
      migrated = true;
      progressed = true;
      break;
    }
  }

  stateTable["_format_version"] = version;
  if (migrated) {
    nlohmann::json migratedState = luaObjectToJson(stateTable);
    migratedState.erase("_format_version");
    (void)host.writeSaveDocument(slot, migratedState.dump(), version);
  }
  return sol::make_object(state, stateTable);
}

bool writeSaveTable(LuaScriptHost& host, const std::string& slot, const sol::table table, const sol::optional<int> version) {
  nlohmann::json state = luaObjectToJson(table);
  int formatVersion = version.value_or(2);
  if (state.contains("_format_version") && state["_format_version"].is_number_integer()) {
    formatVersion = state["_format_version"].get<int>();
    state.erase("_format_version");
  }
  return host.writeSaveDocument(slot, state.dump(), formatVersion);
}

std::uint64_t registerSaveMigration(lua_State* state, LuaScriptHost& host, const int fromVersion, const int toVersion, const sol::function callback) {
  const int ref = callbackRef(state, callback);
  host.addSaveMigrationHook(fromVersion, toVersion, ref);
  return static_cast<std::uint64_t>(ref);
}

PhysicsContactFilter2D contactFilterFromTable(const sol::optional<sol::table> filterTable) {
  PhysicsContactFilter2D filter;
  if (!filterTable.has_value()) {
    return filter;
  }

  const sol::table table = *filterTable;
  if (const sol::object layer = table["layer"]; layer.is<std::string>()) {
    filter.layer = layer.as<std::string>();
  }
  if (const sol::object normalXMin = table["normal_x_min"]; normalXMin.is<float>()) {
    filter.normalXMin = normalXMin.as<float>();
  }
  if (const sol::object normalXMax = table["normal_x_max"]; normalXMax.is<float>()) {
    filter.normalXMax = normalXMax.as<float>();
  }
  if (const sol::object normalYMin = table["normal_y_min"]; normalYMin.is<float>()) {
    filter.normalYMin = normalYMin.as<float>();
  }
  if (const sol::object normalYMax = table["normal_y_max"]; normalYMax.is<float>()) {
    filter.normalYMax = normalYMax.as<float>();
  }
  filter.includeTriggers = table.get_or("include_triggers", false);
  return filter;
}

sol::table contactsTable(lua_State* state, const std::vector<PhysicsContact2D>& contacts) {
  sol::state_view lua(state);
  sol::table result = lua.create_table();
  int index = 1;
  for (const PhysicsContact2D& contact : contacts) {
    sol::table item = lua.create_table();
    item["entity_id"] = contact.entityId;
    item["other_entity_id"] = contact.otherEntityId;
    item["other_layer"] = contact.otherLayer;
    item["normal_x"] = contact.normal.x;
    item["normal_y"] = contact.normal.y;
    item["is_trigger"] = contact.isTrigger;
    result[index++] = item;
  }
  return result;
}

const char* networkEventTypeName(const NetworkEventType type) {
  switch (type) {
  case NetworkEventType::Connected:
    return "connected";
  case NetworkEventType::Disconnected:
    return "disconnected";
  case NetworkEventType::Message:
    return "message";
  }
  return "unknown";
}

sol::table networkEventsTable(lua_State* state, const std::vector<NetworkEvent>& events) {
  sol::state_view lua(state);
  sol::table result = lua.create_table();
  int index = 1;
  for (const NetworkEvent& event : events) {
    sol::table item = lua.create_table();
    item["type"] = networkEventTypeName(event.type);
    item["peer_id"] = event.peerId;
    item["channel"] = event.channel;
    item["message"] = event.message;
    item["latency_ms"] = event.latencyMs;
    result[index++] = item;
  }
  return result;
}

void registerSol2Bindings(LuaScriptHost& host, lua_State* state) {
  sol::state_view lua(state);

  sol::table debug = lua.create_named_table("Debug");
  debug.set_function("log", [](const std::string& message) { std::cout << "[lua] " << message << '\n'; });
  debug.set_function("line", [&host](float x1, float y1, float x2, float y2, sol::optional<float> r, sol::optional<float> g, sol::optional<float> b, sol::optional<float> a) {
      host.addDebugLine(x1, y1, x2, y2, r.value_or(1.0F), g.value_or(1.0F), b.value_or(1.0F), a.value_or(1.0F));
    });
  debug.set_function("clear_lines", [&host] { host.clearDebugLines(); });

  sol::table input = lua.create_named_table("Input");
  input.set_function("is_down", [&host](const std::string& key) { return host.isKeyDown(key); });
  input.set_function("axis", [&host](const std::string& negative, const std::string& positive) { return (host.isKeyDown(positive) ? 1.0F : 0.0F) - (host.isKeyDown(negative) ? 1.0F : 0.0F); });
  input.set_function("vector", [&host](const std::string& left, const std::string& right, const std::string& down, const std::string& up) {
      float x = (host.isKeyDown(right) ? 1.0F : 0.0F) - (host.isKeyDown(left) ? 1.0F : 0.0F);
      float y = (host.isKeyDown(up) ? 1.0F : 0.0F) - (host.isKeyDown(down) ? 1.0F : 0.0F);
      if (x != 0.0F && y != 0.0F) {
        x *= 0.70710678F;
        y *= 0.70710678F;
      }
      return std::tuple{x, y};
    });
  input.set_function("mouse_down", [&host](const std::string& button) { return host.isMouseDown(button); });
  input.set_function("mouse_position", [&host] { const Vec2 value = host.mousePosition(); return std::tuple{value.x, value.y}; });
  input.set_function("mouse_world_position", [&host] { const Vec2 value = host.mouseWorldPosition(); return std::tuple{value.x, value.y}; });
  input.set_function("viewport_size", [&host] { const Vec2 value = host.viewportSize(); return std::tuple{value.x, value.y}; });

  sol::table entity = lua.create_named_table("Entity");
  entity.set_function("find", [&host](const std::string& idOrName) { return host.findEntityId(idOrName); });
  entity.set_function("create", [&host](const std::string& entityId, const sol::table spec) { return host.createEntity(parseEntitySpec(entityId, spec)); });
  entity.set_function("destroy", [&host](const std::string& entityId) { return host.destroyEntity(entityId); });
  entity.set_function("add_position", [&host](const std::string& entityId, float dx, float dy) { (void)host.addEntityPosition(entityId, dx, dy); });
  entity.set_function("set_position", [&host](const std::string& entityId, float x, float y) { return host.setEntityPosition(entityId, x, y); });
  entity.set_function("get_position", [state, &host](const std::string& entityId) { return vec2Result(state, host.entityPosition(entityId)); });

  sol::table transform = lua.create_named_table("Transform");
  transform.set_function("get_position", [state, &host](const std::string& entityId) { return vec2Result(state, host.entityPosition(entityId)); });
  transform.set_function("set_position", [&host](const std::string& entityId, float x, float y) { return host.setEntityPosition(entityId, x, y); });
  transform.set_function("add_position", [&host](const std::string& entityId, float dx, float dy) { return host.addEntityPosition(entityId, dx, dy); });
  transform.set_function("get_rotation", [&host](const std::string& entityId) { return host.entityRotation(entityId); });
  transform.set_function("set_rotation", [&host](const std::string& entityId, float rotation) { return host.setEntityRotation(entityId, rotation); });
  transform.set_function("get_scale", [state, &host](const std::string& entityId) { return vec2Result(state, host.entityScale(entityId)); });
  transform.set_function("set_scale", [&host](const std::string& entityId, float x, float y) { return host.setEntityScale(entityId, x, y); });

  sol::table time = lua.create_named_table("Time");
  time["delta_time"] = 0.0F;

  sol::table timer = lua.create_named_table("Timer");
  timer.set_function("delay", [state, &host](float seconds, const sol::function callback) { return addTimer(state, host, seconds, false, callback); });
  timer.set_function("every", [state, &host](float seconds, const sol::function callback) { return addTimer(state, host, seconds, true, callback); });
  timer.set_function("cancel", [&host](std::uint64_t id) { return host.cancelTimer(id); });

  sol::table events = lua.create_named_table("Events");
  events.set_function("subscribe", [state, &host](const std::string& eventName, const sol::function callback) { return addEventSubscription(state, host, eventName, callback); });
  events.set_function("unsubscribe", [&host](std::uint64_t id) { return host.removeEventSubscription(id); });
  events.set_function("emit", [state, &host](const std::string& eventName, sol::optional<sol::object> payload) { return emitEvent(state, host, eventName, payload.value_or(sol::nil)); });

  sol::table scene = lua.create_named_table("Scene");
  scene.set_function("load", [&host](const std::string& sceneId) { host.requestSceneLoad(sceneId); return true; });

  sol::table runtime = lua.create_named_table("Runtime");
  runtime.set_function("quit", [&host] { host.requestQuit(); });
  runtime.set_function("set_physics_enabled", [&host](bool enabled) { host.setPhysicsEnabled(enabled); });
  runtime.set_function("set_window_mode", [&host](const std::string& mode) { host.setWindowMode(mode); });
  runtime.set_function("get_window_mode", [&host] { return host.windowMode(); });

  sol::table rigidbody = lua.create_named_table("Rigidbody2D");
  rigidbody.set_function("get_velocity", [state, &host](const std::string& entityId) { return vec2Result(state, host.getRigidbodyVelocity(entityId)); });
  rigidbody.set_function("set_velocity", [&host](const std::string& entityId, float x, float y) { return host.setRigidbodyVelocity(entityId, x, y); });
  rigidbody.set_function("set_velocity_x", [&host](const std::string& entityId, float x) { return host.setRigidbodyVelocityX(entityId, x); });
  rigidbody.set_function("set_velocity_y", [&host](const std::string& entityId, float y) { return host.setRigidbodyVelocityY(entityId, y); });
  rigidbody.set_function("add_impulse", [&host](const std::string& entityId, float x, float y) { return host.addRigidbodyImpulse(entityId, x, y); });

  sol::table physics = lua.create_named_table("Physics2D");
  physics.set_function("overlap_box", [&host](float x, float y, float width, float height, sol::optional<std::string> ignoredEntityId) { return host.physicsOverlapBox(x, y, width, height, ignoredEntityId.value_or("")); });
  physics.set_function("has_contact", [&host](const std::string& entityId, sol::optional<sol::table> filter) { return host.physicsHasContact(entityId, contactFilterFromTable(filter)); });
  physics.set_function("contacts", [state, &host](const std::string& entityId) { return contactsTable(state, host.physicsContacts(entityId)); });

  sol::table hud = lua.create_named_table("Hud");
  hud.set_function("text", [&host](const std::string& id, const std::string& text, float x, float y, sol::optional<float> scale, sol::optional<float> r, sol::optional<float> g, sol::optional<float> b, sol::optional<float> a) { return host.createHudText(id, text, x, y, scale.value_or(3.0F), Color{r.value_or(1.0F), g.value_or(1.0F), b.value_or(1.0F), a.value_or(1.0F)}); });
  hud.set_function("rect", [&host](const std::string& id, float x, float y, float width, float height, sol::optional<float> r, sol::optional<float> g, sol::optional<float> b, sol::optional<float> a) { return host.createHudRect(id, x, y, width, height, Color{r.value_or(1.0F), g.value_or(1.0F), b.value_or(1.0F), a.value_or(1.0F)}); });
  hud.set_function("set_text", [&host](const std::string& id, const std::string& text) { return host.setHudText(id, text); });
  hud.set_function("set_rect", [&host](const std::string& id, float x, float y, float width, float height) { return host.setHudRect(id, x, y, width, height); });
  hud.set_function("set_color", [&host](const std::string& id, float r, float g, float b, sol::optional<float> a) { return host.setHudColor(id, Color{r, g, b, a.value_or(1.0F)}); });
  hud.set_function("set_visible", [&host](const std::string& id, bool visible) { return host.setHudVisible(id, visible); });
  hud.set_function("set_group_visible", [&host](const std::string& group, bool visible) { return host.setHudGroupVisible(group, visible); });
  hud.set_function("get_text", [&host](const std::string& id) { return host.hudText(id); });

  sol::table save = lua.create_named_table("Save");
  save.set_function("get_number", [&host](const std::string& slot, const std::string& key, sol::optional<float> fallback) { return host.saveNumber(slot, key).value_or(fallback.value_or(0.0F)); });
  save.set_function("set_number", [&host](const std::string& slot, const std::string& key, float value) { return host.setSaveNumber(slot, key, value); });
  save.set_function("get_string", [&host](const std::string& slot, const std::string& key, sol::optional<std::string> fallback) { return host.saveString(slot, key).value_or(fallback.value_or("")); });
  save.set_function("set_string", [&host](const std::string& slot, const std::string& key, const std::string& value) { return host.setSaveString(slot, key, value); });
  save.set_function("read", [state, &host](const std::string& slot) { return readSaveTable(state, host, slot); });
  save.set_function("write", [&host](const std::string& slot, const sol::table table, sol::optional<int> version) { return writeSaveTable(host, slot, table, version); });
  save.set_function("exists", [&host](const std::string& slot) { return host.saveExists(slot); });
  save.set_function("delete", [&host](const std::string& slot) { return host.deleteSave(slot); });
  save.set_function("version", [&host](const std::string& slot) { return host.saveFormatVersion(slot); });
  save.set_function("register_migration", [state, &host](int fromVersion, int toVersion, const sol::function callback) { return registerSaveMigration(state, host, fromVersion, toVersion, callback); });

  sol::table audio = lua.create_named_table("Audio");
  audio.set_function("play", [&host](const std::string& assetId) { return host.playAudio(assetId); });
  audio.set_function("stop", [&host](std::uint64_t handle) { return host.stopAudio(handle); });
  audio.set_function("set_master_volume", [&host](float volume) { host.setMasterVolume(volume); });
  audio.set_function("get_master_volume", [&host] { return host.masterVolume(); });

  sol::table audioSource = lua.create_named_table("AudioSource");
  audioSource.set_function("play", [&host](const std::string& entityId) { return host.playAudioSource(entityId); });
  audioSource.set_function("stop", [&host](const std::string& entityId) { return host.stopAudioSource(entityId); });

  sol::table video = lua.create_named_table("Video");
  video.set_function("play", [&host](const std::string& assetId, sol::optional<bool> loop) { return host.playVideo(assetId, loop.value_or(false)); });
  video.set_function("play_component", [&host](const std::string& entityId) { return host.playVideoPlayer(entityId); });
  video.set_function("stop", [&host](std::uint64_t handle) { return host.stopVideo(handle); });
  video.set_function("is_playing", [&host](std::uint64_t handle) { return host.isVideoPlaying(handle); });

  sol::table cutscene = lua.create_named_table("Cutscene");
  cutscene.set_function("play", [&host](const std::string& id) { return host.startCutscene(id); });
  cutscene.set_function("pause", [&host] { return host.pauseCutscene(); });
  cutscene.set_function("resume", [&host] { return host.resumeCutscene(); });
  cutscene.set_function("skip", [&host] { return host.stopCutscene(); });
  cutscene.set_function("stop", [&host] { return host.stopCutscene(); });
  cutscene.set_function("is_playing", [&host] { return host.isCutscenePlaying(); });
  cutscene.set_function("active", [&host] { return host.activeCutscene(); });

  sol::table network = lua.create_named_table("Network");
  network.set_function("available", [&host] { return host.networkAvailable(); });
  network.set_function("host", [&host](int port, sol::optional<int> maxPeers) { return host.networkHost(static_cast<std::uint16_t>(std::max(port, 0)), static_cast<std::uint32_t>(std::max(maxPeers.value_or(8), 1))); });
  network.set_function("connect", [&host](const std::string& address, int port) { return host.networkConnect(address, static_cast<std::uint16_t>(std::max(port, 0))); });
  network.set_function("disconnect", [&host] { host.networkDisconnect(); });
  network.set_function("send", [&host](const std::string& message, sol::optional<bool> reliable, sol::optional<int> peerId, sol::optional<int> channel) {
      return host.networkSend(message, reliable.value_or(true), static_cast<std::uint8_t>(std::max(channel.value_or(0), 0)), static_cast<std::uint32_t>(std::max(peerId.value_or(0), 0)));
    });
  network.set_function("is_host", [&host] { return host.networkIsHost(); });
  network.set_function("is_connected", [&host] { return host.networkIsConnected(); });
  network.set_function("latency_ms", [&host] { return host.networkLatencyMs(); });
  network.set_function("events", [state, &host] { return networkEventsTable(state, host.networkDrainEvents()); });
}

} // namespace

bool luaCall(lua_State* state, const int argCount, const int resultCount, std::string& error) {
  const int functionIndex = lua_gettop(state) - argCount;
  lua_pushcfunction(state, luaTraceback);
  lua_insert(state, functionIndex);
  const int status = lua_pcall(state, argCount, resultCount, functionIndex);
  lua_remove(state, functionIndex);
  if (status != LUA_OK) {
    error = lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1) : "Lua callback failed.";
    lua_pop(state, 1);
    return false;
  }
  return true;
}

void luaReportCallbackError(const char* functionName, const std::filesystem::path& path, const std::string& ownerId, const std::string& error) {
  std::cerr << "Lua " << functionName << " failed";
  if (!path.empty()) {
    std::cerr << " in " << path.string();
  }
  if (!ownerId.empty()) {
    std::cerr << " for " << ownerId;
  }
  std::cerr << ": " << error << '\n';
}

std::filesystem::path luaResolveScriptPath(const ProjectData& project, const std::string& module) {
  constexpr std::string_view prefix = "script://";
  return module.starts_with(prefix) ? project.projectDirectory / module.substr(prefix.size()) : project.projectDirectory / module;
}

void luaConfigurePackagePath(lua_State* state, const ProjectData& project) {
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
  const std::string path = scripts.string() + "/?.lua;" + scripts.string() + "/?/init.lua;" + project.projectDirectory.string() + "/?.lua;" + project.projectDirectory.string() + "/?/init.lua;" + current;
  lua_pushstring(state, path.c_str());
  lua_setfield(state, -2, "path");
  lua_pop(state, 1);
}

std::filesystem::file_time_type luaScriptWriteTime(const std::filesystem::path& path) {
  std::error_code error;
  const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, error);
  return error ? std::filesystem::file_time_type{} : time;
}

bool luaLoadScriptTable(lua_State* state, const std::filesystem::path& scriptPath, std::string& error) {
  if (luaL_loadfile(state, scriptPath.string().c_str()) != LUA_OK) {
    error = lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1) : "Lua syntax error.";
    lua_pop(state, 1);
    return false;
  }
  if (!luaCall(state, 0, 1, error)) {
    return false;
  }
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    error = "script must return a table";
    return false;
  }
  return true;
}

void luaCallLifecycle(lua_State* state, const int tableRef, const char* functionName, const std::filesystem::path& path, const std::string& ownerId, const float dt) {
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
  std::string error;
  if (!luaCall(state, argCount, 0, error)) {
    luaReportCallbackError(functionName, path, ownerId, error);
  }
  lua_pop(state, 1);
}

void luaCallUiEvent(lua_State* state, const int tableRef, const char* functionName, const HudButtonElement& button, const Vec2 mousePosition, const std::filesystem::path& path) {
  lua_rawgeti(state, LUA_REGISTRYINDEX, tableRef);
  lua_getfield(state, -1, functionName);
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }
  lua_pushvalue(state, -2);
  lua_newtable(state);
  lua_pushstring(state, button.id.c_str()); lua_setfield(state, -2, "id");
  lua_pushstring(state, button.label.c_str()); lua_setfield(state, -2, "label");
  lua_pushnumber(state, mousePosition.x); lua_setfield(state, -2, "mouse_x");
  lua_pushnumber(state, mousePosition.y); lua_setfield(state, -2, "mouse_y");
  std::string error;
  if (!luaCall(state, 2, 0, error)) {
    luaReportCallbackError(functionName, path, button.id, error);
  }
  lua_pop(state, 1);
}

bool luaRegisterBindings(LuaScriptHost& host, lua_State* state, std::string& error) {
  if (state == nullptr) {
    error = "Lua state was not initialized.";
    return false;
  }
  registerSol2Bindings(host, state);
  return true;
}

#endif

} // namespace demi::runtime
