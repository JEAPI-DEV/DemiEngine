#include "demi/runtime/scripting/LuaScriptHostInternal.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <vector>

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

std::tuple<sol::object, sol::object, sol::object> vec3Result(lua_State* state, const std::optional<Vec3> value) {
  if (!value.has_value()) {
    return {sol::nil, sol::nil, sol::nil};
  }
  return {sol::make_object(state, value->x), sol::make_object(state, value->y), sol::make_object(state, value->z)};
}

Vec2 vec2Field(const sol::table table, const char* fieldName, const Vec2 fallback = {}) {
  const sol::object object = table[fieldName];
  if (!object.is<sol::table>()) {
    return fallback;
  }
  const sol::table vec = object.as<sol::table>();
  return Vec2{.x = vec.get_or(1, fallback.x), .y = vec.get_or(2, fallback.y)};
}

Vec3 vec3Field(const sol::table table, const char* fieldName, const Vec3 fallback = {}) {
  const sol::object object = table[fieldName];
  if (!object.is<sol::table>()) {
    return fallback;
  }
  const sol::table vec = object.as<sol::table>();
  return Vec3{.x = vec.get_or(1, fallback.x), .y = vec.get_or(2, fallback.y), .z = vec.get_or(3, fallback.z)};
}

Color colorField(const sol::table table, const char* fieldName, const Color fallback = {1.0F, 1.0F, 1.0F, 1.0F}) {
  const sol::object object = table[fieldName];
  if (!object.is<sol::table>()) {
    return fallback;
  }
  const sol::table color = object.as<sol::table>();
  const auto channel = [&](const int index, const float channelFallback) {
    const sol::object numeric = color[index];
    if (numeric.is<double>() || numeric.is<int>()) {
      return numeric.as<float>();
    }
    const sol::object string = color[std::to_string(index)];
    return (string.is<double>() || string.is<int>()) ? string.as<float>() : channelFallback;
  };
  return Color{
    .r = channel(1, fallback.r),
    .g = channel(2, fallback.g),
    .b = channel(3, fallback.b),
    .a = channel(4, fallback.a),
  };
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
      .parent = transform.get_or("parent", std::string{}),
      .position = vec2Field(transform, "position"),
      .rotation = transform.get_or("rotation", 0.0F),
      .scale = vec2Field(transform, "scale", {1.0F, 1.0F}),
    };
  }

  if (const sol::table transform = componentTable(components, "Transform3D"); transform.valid()) {
    entity.transform3D = Transform3DComponent{
      .parent = transform.get_or("parent", std::string{}),
      .position = vec3Field(transform, "position"),
      .rotation = vec3Field(transform, "rotation"),
      .scale = vec3Field(transform, "scale", {1.0F, 1.0F, 1.0F}),
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
      .color = colorField(sprite, "color"),
    };
  }

  if (const sol::table mesh = componentTable(components, "MeshRenderer"); mesh.valid()) {
    entity.meshRenderer = MeshRendererComponent{
      .shape = mesh.get_or("shape", std::string("cube")),
      .size = vec3Field(mesh, "size", {1.0F, 1.0F, 1.0F}),
      .color = colorField(mesh, "color", {0.8F, 0.8F, 0.8F, 1.0F}),
      .texture = mesh.get_or("texture", std::string{}),
      .wireframe = mesh.get_or("wireframe", false),
    };
  }

  if (const sol::table collider = componentTable(components, "BoxCollider3D"); collider.valid()) {
    entity.boxCollider3D = BoxCollider3DComponent{
      .size = vec3Field(collider, "size", {1.0F, 1.0F, 1.0F}),
      .offset = vec3Field(collider, "offset"),
      .isTrigger = collider.get_or("is_trigger", false),
      .layer = collider.get_or("layer", std::string{}),
    };
  }

  if (const sol::table collider = componentTable(components, "SphereCollider3D"); collider.valid()) {
    entity.sphereCollider3D = SphereCollider3DComponent{
      .radius = collider.get_or("radius", 0.5F),
      .offset = vec3Field(collider, "offset"),
      .isTrigger = collider.get_or("is_trigger", false),
      .layer = collider.get_or("layer", std::string{}),
    };
  }

  if (const sol::table rigidbody = componentTable(components, "Rigidbody3D"); rigidbody.valid()) {
    entity.rigidbody3D = Rigidbody3DComponent{
      .bodyType = rigidbody.get_or("body_type", std::string("dynamic")),
      .velocity = vec3Field(rigidbody, "velocity"),
      .useGravity = rigidbody.get_or("use_gravity", true),
      .gravityScale = rigidbody.get_or("gravity_scale", 1.0F),
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
    const sol::table table = object.as<sol::table>();
    bool arrayLike = true;
    int maxIndex = 0;
    std::vector<std::pair<int, sol::object>> indexedItems;
    for (const auto& [key, item] : table) {
      if (!key.is<int>() || key.as<int>() <= 0) {
        arrayLike = false;
        break;
      }
      const int index = key.as<int>();
      maxIndex = std::max(maxIndex, index);
      indexedItems.push_back({index, item});
    }
    if (arrayLike && !indexedItems.empty() && maxIndex == static_cast<int>(indexedItems.size())) {
      std::ranges::sort(indexedItems, {}, &std::pair<int, sol::object>::first);
      nlohmann::json value = nlohmann::json::array();
      for (const auto& [_, item] : indexedItems) {
        value.push_back(luaObjectToJson(item));
      }
      return value;
    }

    nlohmann::json value = nlohmann::json::object();
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

std::string encodeNetworkMessage(const std::string& type, const sol::optional<sol::object> payload) {
  nlohmann::json message = nlohmann::json::object();
  message["type"] = type;
  message["payload"] = payload.has_value() ? luaObjectToJson(*payload) : nlohmann::json::object();
  return message.dump();
}

struct NetworkSessionClaimObject {
  bool pending = false;
  sol::function onRemoved;
  sol::function onClaimedLocal;
  sol::function canClaim;
};

struct NetworkSessionRemote {
  std::string senderId;
  float x = 0.0F;
  float y = 0.0F;
  float vx = 0.0F;
  float vy = 0.0F;
  float age = 0.0F;
};

struct NetworkSessionState {
  float sendInterval = 1.0F / 60.0F;
  float extrapolationLimit = 0.10F;
  float initialPrediction = 0.025F;
  std::uint8_t channel = 1;
  std::uint16_t defaultPort = 39420;
  std::uint32_t maxPeers = 8;
  float accumulator = 0.0F;
  std::string localPeerId;
  Color localColor = {1.0F, 1.0F, 1.0F, 1.0F};
  sol::object sessionMetadata = sol::nil;
  sol::table remotePrefab;
  std::unordered_map<std::string, NetworkSessionRemote> remotes;
  std::unordered_map<std::string, NetworkSessionClaimObject> claimObjects;
  std::unordered_map<std::string, std::string> claimedObjects;
};

std::string networkSessionSenderId(LuaScriptHost& host, const NetworkSessionState& session) {
  if (host.networkIsHost()) {
    return "host";
  }
  return session.localPeerId.empty() ? "client" : session.localPeerId;
}

std::string networkSessionRemoteId(const sol::table snapshot) {
  return "net_" + snapshot.get_or("sender_id", std::string{}) + "_" + snapshot.get_or("entity_id", std::string{});
}

void networkSessionReset(LuaScriptHost& host, NetworkSessionState& session, const bool clearRemoteEntities) {
  if (clearRemoteEntities) {
    for (const auto& [ghostId, _] : session.remotes) {
      (void)host.destroyEntity(ghostId);
    }
  }
  session.localPeerId.clear();
  session.accumulator = 0.0F;
  session.remotes.clear();
  session.claimObjects.clear();
  session.claimedObjects.clear();
  session.sessionMetadata = sol::object{};
}

sol::table networkSessionClaimSyncPayload(lua_State* state, const NetworkSessionState& session) {
  sol::state_view lua(state);
  sol::table payload = lua.create_table();
  sol::table claims = lua.create_table();
  int index = 1;
  for (const auto& [objectId, collectorId] : session.claimedObjects) {
    sol::table claim = lua.create_table();
    claim["object_id"] = objectId;
    claim["collector_id"] = collectorId;
    claims[index++] = claim;
  }
  payload["claims"] = claims;
  return payload;
}

bool networkSessionSendMessage(lua_State* state, LuaScriptHost& host, const std::string& type, const sol::object payload, const bool reliable = true, const std::uint32_t peerId = 0, const std::uint8_t channel = 0) {
  (void)state;
  return host.networkSend(encodeNetworkMessage(type, sol::optional<sol::object>(payload)), reliable, channel, peerId);
}

bool networkSessionSendClaimSync(lua_State* state, LuaScriptHost& host, const NetworkSessionState& session, const std::uint32_t peerId = 0) {
  if (!host.networkAvailable() || !host.networkIsHost()) {
    return false;
  }
  sol::object payload = sol::make_object(state, networkSessionClaimSyncPayload(state, session));
  return networkSessionSendMessage(state, host, "claim_once_sync", payload, true, peerId, 0);
}

void networkSessionApplySnapshot(LuaScriptHost& host, NetworkSessionState& session, const sol::table snapshot) {
  const std::string senderId = snapshot.get_or("sender_id", std::string{});
  if (senderId.empty() || senderId == networkSessionSenderId(host, session) || !snapshot["x"].valid() || !snapshot["y"].valid()) {
    return;
  }

  const std::string ghostId = networkSessionRemoteId(snapshot);
  const bool needsCreate = !session.remotes.contains(ghostId) || !host.findEntityId(ghostId).has_value();
  if (needsCreate) {
    if (session.remotePrefab.valid()) {
      Entity entity;
      entity.id = ghostId;
      entity.name = session.remotePrefab.get_or("name", std::string("Network Ghost"));
      entity.transform2D = Transform2DComponent{
        .parent = session.remotePrefab.get_or("parent", std::string{}),
        .position = Vec2{.x = snapshot.get_or("x", 0.0F), .y = snapshot.get_or("y", 0.0F)},
        .rotation = session.remotePrefab.get_or("rotation", 0.0F),
        .scale = vec2Field(session.remotePrefab, "scale", {1.0F, 1.0F}),
      };
      entity.sprite = SpriteComponent{
        .texture = session.remotePrefab.get_or("texture", std::string{}),
        .layer = session.remotePrefab.get_or("layer", std::string("network")),
        .color = colorField(snapshot, "color", colorField(session.remotePrefab, "color")),
      };
      (void)host.createEntity(std::move(entity));
    }
    session.remotes[ghostId] = NetworkSessionRemote{.senderId = senderId};
  }

  NetworkSessionRemote& remote = session.remotes[ghostId];
  remote.senderId = senderId;
  remote.x = snapshot.get_or("x", 0.0F);
  remote.y = snapshot.get_or("y", 0.0F);
  remote.vx = snapshot.get_or("vx", 0.0F);
  remote.vy = snapshot.get_or("vy", 0.0F);
  remote.age = 0.0F;
  if (snapshot["color"].is<sol::table>()) {
    (void)host.setEntitySpriteColor(ghostId, colorField(snapshot, "color", colorField(session.remotePrefab, "color")));
  }
  (void)host.setEntityPosition(ghostId, remote.x + remote.vx * session.initialPrediction, remote.y + remote.vy * session.initialPrediction);
}

bool networkSessionApplyClaimOnce(lua_State* state, LuaScriptHost& host, NetworkSessionState& session, const std::string& id, const std::string& collectorId, const bool broadcast, const sol::object claim) {
  if (id.empty() || session.claimedObjects.contains(id)) {
    return false;
  }

  auto object = session.claimObjects.find(id);
  if (broadcast && object != session.claimObjects.end() && object->second.canClaim.valid()) {
    const sol::protected_function canClaim = object->second.canClaim;
    const sol::protected_function_result result = canClaim(id, collectorId, claim);
    if (!result.valid() || !result.get<bool>()) {
      return false;
    }
  }

  session.claimedObjects[id] = collectorId;
  if (object != session.claimObjects.end()) {
    object->second.pending = false;
    if (object->second.onRemoved.valid()) {
      const sol::protected_function onRemoved = object->second.onRemoved;
      (void)onRemoved(id, collectorId);
    } else {
      (void)host.destroyEntity(id);
    }
    if (collectorId == networkSessionSenderId(host, session) && object->second.onClaimedLocal.valid()) {
      const sol::protected_function onClaimedLocal = object->second.onClaimedLocal;
      (void)onClaimedLocal(id, collectorId);
    }
  }

  if (broadcast && host.networkAvailable() && host.networkIsHost()) {
    sol::state_view lua(state);
    sol::table payload = lua.create_table();
    payload["object_id"] = id;
    payload["collector_id"] = collectorId;
    networkSessionSendMessage(state, host, "claim_once_claimed", sol::make_object(state, payload), true, 0, 0);
  }
  return true;
}

sol::object decodeNetworkMessage(lua_State* state, const std::string& text) {
  try {
    const nlohmann::json message = nlohmann::json::parse(text);
    if (!message.is_object() || !message.contains("type") || !message["type"].is_string()) {
      return sol::nil;
    }
    sol::state_view lua(state);
    sol::table result = lua.create_table();
    result["type"] = message["type"].get<std::string>();
    result["payload"] = jsonToLuaObject(state, message.value("payload", nlohmann::json::object()));
    return sol::make_object(state, result);
  } catch (...) {
    return sol::nil;
  }
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
  entity.set_function("set_sprite_color", [&host](const std::string& entityId, float r, float g, float b, sol::optional<float> a) { return host.setEntitySpriteColor(entityId, Color{r, g, b, a.value_or(1.0F)}); });
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

  sol::table transform3d = lua.create_named_table("Transform3D");
  transform3d.set_function("get_position", [state, &host](const std::string& entityId) { return vec3Result(state, host.entityPosition3D(entityId)); });
  transform3d.set_function("set_position", [&host](const std::string& entityId, float x, float y, float z) { return host.setEntityPosition3D(entityId, x, y, z); });
  transform3d.set_function("add_position", [&host](const std::string& entityId, float dx, float dy, float dz) { return host.addEntityPosition3D(entityId, dx, dy, dz); });
  transform3d.set_function("get_rotation", [state, &host](const std::string& entityId) { return vec3Result(state, host.entityRotation3D(entityId)); });
  transform3d.set_function("set_rotation", [&host](const std::string& entityId, float x, float y, float z) { return host.setEntityRotation3D(entityId, x, y, z); });
  transform3d.set_function("get_scale", [state, &host](const std::string& entityId) { return vec3Result(state, host.entityScale3D(entityId)); });
  transform3d.set_function("set_scale", [&host](const std::string& entityId, float x, float y, float z) { return host.setEntityScale3D(entityId, x, y, z); });

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
  runtime.set_function("set_max_fps", [&host](double maxFps) { host.setMaxFps(static_cast<int>(std::round(maxFps))); });
  runtime.set_function("get_max_fps", [&host] { return host.maxFps(); });

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
  hud.set_function("set_button_label", [&host](const std::string& id, const std::string& label) { return host.setHudButtonLabel(id, label); });
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
  network.set_function("sender_id", [&host](sol::optional<std::string> assignedPeerId) {
      if (host.networkIsHost()) {
        return std::string("host");
      }
      return assignedPeerId.value_or("client");
    });
  network.set_function("encode", [](const std::string& type, sol::optional<sol::object> payload) { return encodeNetworkMessage(type, payload); });
  network.set_function("decode", [state](const std::string& message) { return decodeNetworkMessage(state, message); });

  auto session = std::make_shared<NetworkSessionState>();
  sol::table networkSession = lua.create_named_table("NetworkSession");
  networkSession.set_function("configure", [session](const sol::table options) {
      session->sendInterval = options.get_or("send_interval", session->sendInterval);
      session->extrapolationLimit = options.get_or("extrapolation_limit", session->extrapolationLimit);
      session->initialPrediction = options.get_or("initial_prediction", session->initialPrediction);
      session->channel = static_cast<std::uint8_t>(std::max(options.get_or("channel", static_cast<int>(session->channel)), 0));
      session->defaultPort = static_cast<std::uint16_t>(std::max(options.get_or("port", static_cast<int>(session->defaultPort)), 0));
      session->maxPeers = static_cast<std::uint32_t>(std::max(options.get_or("max_peers", static_cast<int>(session->maxPeers)), 1));
      const sol::object remotePrefab = options["remote_prefab"];
      if (remotePrefab.is<sol::table>()) {
        session->remotePrefab = remotePrefab.as<sol::table>();
      }
    });
  networkSession.set_function("sender_id", [&host, session] { return networkSessionSenderId(host, *session); });
  networkSession.set_function("set_local_color", [session](float r, float g, float b, sol::optional<float> a) {
      session->localColor = Color{.r = r, .g = g, .b = b, .a = a.value_or(1.0F)};
    });
  networkSession.set_function("host", [&host, session](sol::optional<int> port) {
      if (!host.networkAvailable()) {
        return false;
      }
      networkSessionReset(host, *session, true);
      return host.networkHost(static_cast<std::uint16_t>(std::max(port.value_or(session->defaultPort), 0)), session->maxPeers);
    });
  networkSession.set_function("connect", [&host, session](sol::optional<std::string> address, sol::optional<int> port) {
      if (!host.networkAvailable()) {
        return false;
      }
      networkSessionReset(host, *session, true);
      return host.networkConnect(address.value_or("127.0.0.1"), static_cast<std::uint16_t>(std::max(port.value_or(session->defaultPort), 0)));
    });
  networkSession.set_function("disconnect", [&host, session] {
      host.networkDisconnect();
      networkSessionReset(host, *session, true);
    });
  networkSession.set_function("is_connected", [&host] { return host.networkIsConnected(); });
  networkSession.set_function("start_session", [state, &host, session](const sol::object metadata) {
      session->sessionMetadata = metadata;
      if (metadata.valid() && metadata != sol::nil && host.networkAvailable() && host.networkIsHost()) {
        networkSessionSendMessage(state, host, "session_start", metadata, true, 0, 0);
      }
    });
  networkSession.set_function("current_session", [session] { return session->sessionMetadata; });
  networkSession.set_function("reset_claims", [session] {
      session->claimObjects.clear();
      session->claimedObjects.clear();
    });
  networkSession.set_function("remote_position", [state, session](const std::string& senderId) {
      const auto best = std::ranges::min_element(session->remotes, [&](const auto& left, const auto& right) {
        if (left.second.senderId != senderId) {
          return false;
        }
        if (right.second.senderId != senderId) {
          return true;
        }
        return left.second.age < right.second.age;
      });
      if (best == session->remotes.end() || best->second.senderId != senderId) {
        return std::tuple<sol::object, sol::object>{sol::nil, sol::nil};
      }
      return std::tuple<sol::object, sol::object>{sol::make_object(state, best->second.x), sol::make_object(state, best->second.y)};
    });
  networkSession.set_function("register_claim_once", [session](const std::string& id, sol::optional<sol::table> options) {
      if (id.empty()) {
        return false;
      }
      NetworkSessionClaimObject object;
      object.pending = false;
      if (options.has_value()) {
        const sol::table table = *options;
        const sol::object onRemoved = table["on_removed"];
        if (onRemoved.is<sol::function>()) {
          object.onRemoved = onRemoved.as<sol::function>();
        }
        const sol::object onClaimedLocal = table["on_claimed_local"];
        if (onClaimedLocal.is<sol::function>()) {
          object.onClaimedLocal = onClaimedLocal.as<sol::function>();
        }
        const sol::object canClaim = table["can_claim"];
        if (canClaim.is<sol::function>()) {
          object.canClaim = canClaim.as<sol::function>();
        }
      }
      const bool alreadyClaimed = session->claimedObjects.contains(id);
      session->claimObjects[id] = object;
      return !alreadyClaimed;
    });
  networkSession.set_function("apply_claim_once", [state, &host, session](const std::string& id, const std::string& collectorId, sol::optional<bool> broadcast, sol::optional<sol::object> claim) {
      return networkSessionApplyClaimOnce(state, host, *session, id, collectorId, broadcast.value_or(false), claim.value_or(sol::make_object(state, sol::nil)));
    });
  networkSession.set_function("request_claim_once_sync", [state, &host](sol::optional<int> peerId) {
      if (!host.networkAvailable()) {
        return false;
      }
      sol::state_view lua(state);
      return host.networkSend(encodeNetworkMessage("claim_once_sync_request", sol::make_object(state, lua.create_table())), true, 0, static_cast<std::uint32_t>(std::max(peerId.value_or(0), 0)));
    });
  networkSession.set_function("try_claim_once", [state, &host, session](const std::string& id, sol::optional<sol::object> claim) {
      auto object = session->claimObjects.find(id);
      if (object == session->claimObjects.end() || object->second.pending || session->claimedObjects.contains(id)) {
        return false;
      }
      const sol::object claimObject = claim.value_or(sol::make_object(state, sol::nil));
      if (!host.networkAvailable() || host.networkIsHost() || !host.networkIsConnected()) {
        return networkSessionApplyClaimOnce(state, host, *session, id, networkSessionSenderId(host, *session), true, claimObject);
      }
      object->second.pending = true;
      sol::state_view lua(state);
      sol::table payload = claimObject.is<sol::table>() ? claimObject.as<sol::table>() : lua.create_table();
      payload["object_id"] = id;
      return networkSessionSendMessage(state, host, "claim_once_request", sol::make_object(state, payload), true, 0, 0);
    });
  networkSession.set_function("process_events", [state, &host, session] {
      sol::state_view lua(state);
      sol::table summary = lua.create_table();
      summary["connected"] = false;
      summary["disconnected"] = false;
      summary["session_started"] = false;
      summary["session"] = sol::nil;
      summary["messages"] = 0;
      if (!host.networkAvailable()) {
        return summary;
      }
      int messages = 0;
      for (const NetworkEvent& event : host.networkDrainEvents()) {
        if (event.type == NetworkEventType::Connected) {
          summary["connected"] = true;
          if (host.networkIsHost()) {
            sol::table assign = lua.create_table();
            assign["peer_id"] = "peer" + std::to_string(event.peerId);
            networkSessionSendMessage(state, host, "assign_peer", sol::make_object(state, assign), true, event.peerId, 0);
            if (session->sessionMetadata.valid() && session->sessionMetadata != sol::nil) {
              networkSessionSendMessage(state, host, "session_start", session->sessionMetadata, true, event.peerId, 0);
            }
            networkSessionSendClaimSync(state, host, *session, event.peerId);
          }
        } else if (event.type == NetworkEventType::Disconnected) {
          summary["disconnected"] = true;
          if (host.networkIsHost()) {
            const std::string senderId = "peer" + std::to_string(event.peerId);
            for (auto it = session->remotes.begin(); it != session->remotes.end();) {
              if (it->second.senderId == senderId) {
                (void)host.destroyEntity(it->first);
                it = session->remotes.erase(it);
              } else {
                ++it;
              }
            }
          } else {
            networkSessionReset(host, *session, true);
          }
        } else if (event.type == NetworkEventType::Message) {
          ++messages;
          const sol::object decoded = decodeNetworkMessage(state, event.message);
          if (!decoded.is<sol::table>()) {
            continue;
          }
          const sol::table message = decoded.as<sol::table>();
          const std::string type = message.get_or("type", std::string{});
          const sol::object payloadObject = message["payload"];
          sol::table payload = payloadObject.is<sol::table>() ? payloadObject.as<sol::table>() : lua.create_table();
          if (type == "assign_peer") {
            session->localPeerId = payload.get_or("peer_id", std::string{});
          } else if (type == "session_start") {
            session->sessionMetadata = payloadObject;
            summary["session_started"] = true;
            summary["session"] = payloadObject;
          } else if (type == "transform_snapshot") {
            if (host.networkIsHost() && payload.get_or("sender_id", std::string{}) != "host") {
              payload["sender_id"] = "peer" + std::to_string(event.peerId);
              networkSessionSendMessage(state, host, "transform_snapshot", sol::make_object(state, payload), false, 0, session->channel);
            }
            networkSessionApplySnapshot(host, *session, payload);
          } else if (type == "claim_once_request" && host.networkIsHost()) {
            const std::string objectId = payload.get_or("object_id", std::string{});
            const std::string collectorId = "peer" + std::to_string(event.peerId);
            if (const auto claimed = session->claimedObjects.find(objectId); claimed != session->claimedObjects.end()) {
              sol::table claimedPayload = lua.create_table();
              claimedPayload["object_id"] = objectId;
              claimedPayload["collector_id"] = claimed->second;
              networkSessionSendMessage(state, host, "claim_once_claimed", sol::make_object(state, claimedPayload), true, event.peerId, 0);
            } else if (!networkSessionApplyClaimOnce(state, host, *session, objectId, collectorId, true, payloadObject)) {
              sol::table rejectedPayload = lua.create_table();
              rejectedPayload["object_id"] = objectId;
              rejectedPayload["collector_id"] = collectorId;
              networkSessionSendMessage(state, host, "claim_once_rejected", sol::make_object(state, rejectedPayload), true, event.peerId, 0);
            }
          } else if (type == "claim_once_claimed") {
            networkSessionApplyClaimOnce(state, host, *session, payload.get_or("object_id", std::string{}), payload.get_or("collector_id", std::string{}), false, payloadObject);
          } else if (type == "claim_once_rejected") {
            if (auto object = session->claimObjects.find(payload.get_or("object_id", std::string{})); object != session->claimObjects.end()) {
              object->second.pending = false;
            }
          } else if (type == "claim_once_sync") {
            const sol::object claimsObject = payload["claims"];
            if (claimsObject.is<sol::table>()) {
              const sol::table claims = claimsObject.as<sol::table>();
              for (const auto& [_, claimObject] : claims) {
                if (claimObject.is<sol::table>()) {
                  const sol::table claim = claimObject.as<sol::table>();
                  networkSessionApplyClaimOnce(state, host, *session, claim.get_or("object_id", std::string{}), claim.get_or("collector_id", std::string{}), false, claimObject);
                }
              }
            }
          } else if (type == "claim_once_sync_request" && host.networkIsHost()) {
            networkSessionSendClaimSync(state, host, *session, event.peerId);
          }
        }
      }
      summary["messages"] = messages;
      return summary;
    });
  networkSession.set_function("update_local_transform", [state, &host, session](const std::string& entityId, const float dt) {
      if (!host.networkAvailable()) {
        return;
      }
      for (auto& [ghostId, remote] : session->remotes) {
        remote.age = std::min(remote.age + dt, session->extrapolationLimit);
        (void)host.setEntityPosition(ghostId, remote.x + remote.vx * remote.age, remote.y + remote.vy * remote.age);
      }
      if (!host.networkIsHost() && !host.networkIsConnected()) {
        return;
      }
      session->accumulator += dt;
      if (session->accumulator < session->sendInterval) {
        return;
      }
      session->accumulator = 0.0F;
      const std::optional<Vec2> position = host.entityPosition(entityId);
      if (!position.has_value()) {
        return;
      }
      const std::optional<Vec2> velocity = host.getRigidbodyVelocity(entityId);
      sol::state_view lua(state);
      sol::table payload = lua.create_table();
      payload["sender_id"] = networkSessionSenderId(host, *session);
      payload["entity_id"] = entityId;
      payload["x"] = position->x;
      payload["y"] = position->y;
      payload["vx"] = velocity.has_value() ? velocity->x : 0.0F;
      payload["vy"] = velocity.has_value() ? velocity->y : 0.0F;
      sol::table color = lua.create_table();
      color[1] = session->localColor.r;
      color[2] = session->localColor.g;
      color[3] = session->localColor.b;
      color[4] = session->localColor.a;
      payload["color"] = color;
      networkSessionSendMessage(state, host, "transform_snapshot", sol::make_object(state, payload), false, 0, session->channel);
    });
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
  const std::filesystem::path runtimeScripts = std::filesystem::path(DEMI_SOURCE_DIR) / "scripts" / "runtime";
  const std::string path = scripts.string() + "/?.lua;" + scripts.string() + "/?/init.lua;" + project.projectDirectory.string() + "/?.lua;" + project.projectDirectory.string() + "/?/init.lua;" + runtimeScripts.string() + "/?.lua;" + runtimeScripts.string() + "/?/init.lua;" + current;
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
  lua_pushstring(state, button.action.c_str()); lua_setfield(state, -2, "action");
  lua_pushnumber(state, mousePosition.x); lua_setfield(state, -2, "mouse_x");
  lua_pushnumber(state, mousePosition.y); lua_setfield(state, -2, "mouse_y");
  std::string error;
  if (!luaCall(state, 2, 0, error)) {
    luaReportCallbackError(functionName, path, button.id, error);
  }
  lua_pop(state, 1);
}

void luaCallActionEvent(lua_State* state, const int tableRef, const std::string& functionName, const HudButtonElement& button, const Vec2 mousePosition, const std::filesystem::path& path) {
  lua_rawgeti(state, LUA_REGISTRYINDEX, tableRef);
  lua_getfield(state, -1, functionName.c_str());
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }
  lua_pushvalue(state, -2);
  lua_newtable(state);
  lua_pushstring(state, button.id.c_str()); lua_setfield(state, -2, "id");
  lua_pushstring(state, button.label.c_str()); lua_setfield(state, -2, "label");
  lua_pushstring(state, button.action.c_str()); lua_setfield(state, -2, "action");
  lua_pushnumber(state, mousePosition.x); lua_setfield(state, -2, "mouse_x");
  lua_pushnumber(state, mousePosition.y); lua_setfield(state, -2, "mouse_y");
  std::string error;
  if (!luaCall(state, 2, 0, error)) {
    luaReportCallbackError(functionName.c_str(), path, button.action, error);
  }
  lua_pop(state, 1);
}

void luaCallModuleActionEvent(lua_State* state, const std::string& moduleName, const std::string& functionName, const HudButtonElement& button, const Vec2 mousePosition, const std::filesystem::path& path) {
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, "loaded");
  lua_remove(state, -2);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, moduleName.c_str());
  lua_remove(state, -2);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, functionName.c_str());
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }
  lua_newtable(state);
  lua_pushstring(state, button.id.c_str()); lua_setfield(state, -2, "id");
  lua_pushstring(state, button.label.c_str()); lua_setfield(state, -2, "label");
  lua_pushstring(state, button.action.c_str()); lua_setfield(state, -2, "action");
  lua_pushnumber(state, mousePosition.x); lua_setfield(state, -2, "mouse_x");
  lua_pushnumber(state, mousePosition.y); lua_setfield(state, -2, "mouse_y");
  std::string error;
  if (!luaCall(state, 1, 0, error)) {
    luaReportCallbackError(functionName.c_str(), path, button.action, error);
  }
  lua_pop(state, 1);
}

void luaCallScriptEvent(lua_State* state, const int tableRef, const std::string& functionName, const int payloadIndex, const std::filesystem::path& path, const std::string& eventName) {
  lua_rawgeti(state, LUA_REGISTRYINDEX, tableRef);
  lua_getfield(state, -1, functionName.c_str());
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }
  lua_pushvalue(state, -2);
  payloadIndex > 0 ? lua_pushvalue(state, payloadIndex) : lua_newtable(state);
  std::string error;
  if (!luaCall(state, 2, 0, error)) {
    luaReportCallbackError(functionName.c_str(), path, eventName, error);
  }
  lua_pop(state, 1);
}

void luaCallModuleEvent(lua_State* state, const std::string& moduleName, const std::string& functionName, const int payloadIndex, const std::filesystem::path& path, const std::string& eventName) {
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, "loaded");
  lua_remove(state, -2);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, moduleName.c_str());
  lua_remove(state, -2);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, functionName.c_str());
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return;
  }
  payloadIndex > 0 ? lua_pushvalue(state, payloadIndex) : lua_newtable(state);
  std::string error;
  if (!luaCall(state, 1, 0, error)) {
    luaReportCallbackError(functionName.c_str(), path, eventName, error);
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
