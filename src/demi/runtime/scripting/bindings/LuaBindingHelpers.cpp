#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace demi::runtime {

namespace {

sol::table componentTable(const sol::table components, const char* name) {
  const sol::object object = components[name];
  return object.is<sol::table>() ? object.as<sol::table>() : sol::table{};
}

int callbackRef(lua_State* state, const sol::function callback) {
  callback.push();
  return luaL_ref(state, LUA_REGISTRYINDEX);
}

} // namespace

std::tuple<sol::object, sol::object> luaVec2Result(lua_State* state, const std::optional<Vec2> value) {
  if (!value.has_value()) {
    return {sol::nil, sol::nil};
  }
  return {sol::make_object(state, value->x), sol::make_object(state, value->y)};
}

std::tuple<sol::object, sol::object, sol::object> luaVec3Result(lua_State* state, const std::optional<Vec3> value) {
  if (!value.has_value()) {
    return {sol::nil, sol::nil, sol::nil};
  }
  return {sol::make_object(state, value->x), sol::make_object(state, value->y), sol::make_object(state, value->z)};
}

Vec2 luaVec2Field(const sol::table table, const char* fieldName, const Vec2 fallback) {
  const sol::object object = table[fieldName];
  if (!object.is<sol::table>()) {
    return fallback;
  }
  const sol::table vec = object.as<sol::table>();
  return Vec2{.x = vec.get_or(1, fallback.x), .y = vec.get_or(2, fallback.y)};
}

Vec3 luaVec3Field(const sol::table table, const char* fieldName, const Vec3 fallback) {
  const sol::object object = table[fieldName];
  if (!object.is<sol::table>()) {
    return fallback;
  }
  const sol::table vec = object.as<sol::table>();
  return Vec3{.x = vec.get_or(1, fallback.x), .y = vec.get_or(2, fallback.y), .z = vec.get_or(3, fallback.z)};
}

Color luaColorField(const sol::table table, const char* fieldName, const Color fallback) {
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

Entity luaParseEntitySpec(const std::string& entityId, const sol::table spec) {
  Entity entity;
  entity.id = entityId;
  entity.name = spec.get_or("name", entity.id);

  const sol::object componentsObject = spec["components"];
  const sol::table components = componentsObject.is<sol::table>() ? componentsObject.as<sol::table>() : spec;

  if (const sol::table transform = componentTable(components, "Transform2D"); transform.valid()) {
    entity.transform2D = Transform2DComponent{
      .parent = transform.get_or("parent", std::string{}),
      .position = luaVec2Field(transform, "position"),
      .rotation = transform.get_or("rotation", 0.0F),
      .scale = luaVec2Field(transform, "scale", {1.0F, 1.0F}),
    };
  }

  if (const sol::table transform = componentTable(components, "Transform3D"); transform.valid()) {
    entity.transform3D = Transform3DComponent{
      .parent = transform.get_or("parent", std::string{}),
      .position = luaVec3Field(transform, "position"),
      .rotation = luaVec3Field(transform, "rotation"),
      .scale = luaVec3Field(transform, "scale", {1.0F, 1.0F, 1.0F}),
    };
  }

  if (const sol::table rigidbody = componentTable(components, "Rigidbody2D"); rigidbody.valid()) {
    entity.rigidbody2D = Rigidbody2DComponent{
      .bodyType = rigidbody.get_or("body_type", std::string("dynamic")),
      .velocity = luaVec2Field(rigidbody, "velocity"),
      .gravityScale = rigidbody.get_or("gravity_scale", 1.0F),
      .bounciness = rigidbody.get_or("bounciness", 0.0F),
      .lockRotation = rigidbody.get_or("lock_rotation", true),
    };
  }

  if (const sol::table collider = componentTable(components, "BoxCollider2D"); collider.valid()) {
    entity.boxCollider2D = BoxCollider2DComponent{
      .size = luaVec2Field(collider, "size", {1.0F, 1.0F}),
      .offset = luaVec2Field(collider, "offset"),
      .isTrigger = collider.get_or("is_trigger", false),
      .layer = collider.get_or("layer", std::string{}),
    };
  }

  if (const sol::table sprite = componentTable(components, "Sprite"); sprite.valid()) {
    entity.sprite = SpriteComponent{
      .texture = sprite.get_or("texture", std::string{}),
      .shape = sprite.get_or("shape", std::string("rectangle")),
      .layer = sprite.get_or("layer", std::string{}),
      .color = luaColorField(sprite, "color"),
    };
  }

  if (const sol::table mesh = componentTable(components, "MeshRenderer"); mesh.valid()) {
    entity.meshRenderer = MeshRendererComponent{
      .model = mesh.get_or("model", std::string{}),
      .shape = mesh.get_or("shape", std::string("cube")),
      .size = luaVec3Field(mesh, "size", {1.0F, 1.0F, 1.0F}),
      .color = luaColorField(mesh, "color", {0.8F, 0.8F, 0.8F, 1.0F}),
      .texture = mesh.get_or("texture", std::string{}),
      .wireframe = mesh.get_or("wireframe", false),
    };
  }

  if (const sol::table collider = componentTable(components, "BoxCollider3D"); collider.valid()) {
    entity.boxCollider3D = BoxCollider3DComponent{
      .size = luaVec3Field(collider, "size", {1.0F, 1.0F, 1.0F}),
      .offset = luaVec3Field(collider, "offset"),
      .isTrigger = collider.get_or("is_trigger", false),
      .layer = collider.get_or("layer", std::string{}),
    };
  }

  if (const sol::table collider = componentTable(components, "SphereCollider3D"); collider.valid()) {
    entity.sphereCollider3D = SphereCollider3DComponent{
      .radius = collider.get_or("radius", 0.5F),
      .offset = luaVec3Field(collider, "offset"),
      .isTrigger = collider.get_or("is_trigger", false),
      .layer = collider.get_or("layer", std::string{}),
    };
  }

  if (const sol::table rigidbody = componentTable(components, "Rigidbody3D"); rigidbody.valid()) {
    entity.rigidbody3D = Rigidbody3DComponent{
      .bodyType = rigidbody.get_or("body_type", std::string("dynamic")),
      .velocity = luaVec3Field(rigidbody, "velocity"),
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

std::uint64_t luaAddTimer(lua_State* state, LuaScriptHost& host, const float seconds, const bool repeating, const sol::function callback) {
  const int ref = callbackRef(state, callback);
  const std::uint64_t id = host.addTimer(seconds, repeating, ref);
  if (id == 0) {
    luaL_unref(state, LUA_REGISTRYINDEX, ref);
  }
  return id;
}

std::uint64_t luaAddEventSubscription(lua_State* state, LuaScriptHost& host, const std::string& eventName, const sol::function callback) {
  const int ref = callbackRef(state, callback);
  const std::uint64_t id = host.addEventSubscription(eventName, ref);
  if (id == 0) {
    luaL_unref(state, LUA_REGISTRYINDEX, ref);
  }
  return id;
}

int luaEmitEvent(lua_State* state, LuaScriptHost& host, const std::string& eventName, const sol::object payload) {
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

sol::object luaReadSaveTable(lua_State* state, LuaScriptHost& host, const std::string& slot) {
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

bool luaWriteSaveTable(LuaScriptHost& host, const std::string& slot, const sol::table table, const sol::optional<int> version) {
  nlohmann::json state = luaObjectToJson(table);
  int formatVersion = version.value_or(2);
  if (state.contains("_format_version") && state["_format_version"].is_number_integer()) {
    formatVersion = state["_format_version"].get<int>();
    state.erase("_format_version");
  }
  return host.writeSaveDocument(slot, state.dump(), formatVersion);
}

std::uint64_t luaRegisterSaveMigration(lua_State* state, LuaScriptHost& host, const int fromVersion, const int toVersion, const sol::function callback) {
  const int ref = callbackRef(state, callback);
  host.addSaveMigrationHook(fromVersion, toVersion, ref);
  return static_cast<std::uint64_t>(ref);
}

PhysicsContactFilter2D luaContactFilterFromTable(const sol::optional<sol::table> filterTable) {
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

sol::table luaContactsTable(lua_State* state, const std::vector<PhysicsContact2D>& contacts) {
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

} // namespace demi::runtime
