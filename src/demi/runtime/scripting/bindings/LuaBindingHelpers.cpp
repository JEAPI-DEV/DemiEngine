#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <nlohmann/json.hpp>

namespace demi::runtime {

namespace {

int callbackRef(lua_State *state, const sol::function callback) {
  callback.push();
  return luaL_ref(state, LUA_REGISTRYINDEX);
}

} // namespace

std::tuple<sol::object, sol::object>
luaVec2Result(lua_State *state, const std::optional<Vec2> value) {
  if (!value.has_value()) {
    return {sol::nil, sol::nil};
  }
  return {sol::make_object(state, value->x), sol::make_object(state, value->y)};
}

std::tuple<sol::object, sol::object, sol::object>
luaVec3Result(lua_State *state, const std::optional<Vec3> value) {
  if (!value.has_value()) {
    return {sol::nil, sol::nil, sol::nil};
  }
  return {sol::make_object(state, value->x), sol::make_object(state, value->y),
          sol::make_object(state, value->z)};
}

Vec2 luaVec2Field(const sol::table table, const char *fieldName,
                  const Vec2 fallback) {
  const sol::object object = table[fieldName];
  if (!object.is<sol::table>()) {
    return fallback;
  }
  const sol::table vec = object.as<sol::table>();
  return Vec2{.x = vec.get_or(1, fallback.x), .y = vec.get_or(2, fallback.y)};
}

Vec3 luaVec3Field(const sol::table table, const char *fieldName,
                  const Vec3 fallback) {
  const sol::object object = table[fieldName];
  if (!object.is<sol::table>()) {
    return fallback;
  }
  const sol::table vec = object.as<sol::table>();
  return Vec3{.x = vec.get_or(1, fallback.x),
              .y = vec.get_or(2, fallback.y),
              .z = vec.get_or(3, fallback.z)};
}

Color luaColorField(const sol::table table, const char *fieldName,
                    const Color fallback) {
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
    return (string.is<double>() || string.is<int>()) ? string.as<float>()
                                                     : channelFallback;
  };
  return Color{
      .r = channel(1, fallback.r),
      .g = channel(2, fallback.g),
      .b = channel(3, fallback.b),
      .a = channel(4, fallback.a),
  };
}

std::uint64_t luaAddTimer(lua_State *state, LuaScriptHost &host,
                          const float seconds, const bool repeating,
                          const sol::function callback) {
  const int ref = callbackRef(state, callback);
  const std::uint64_t id = host.addTimer(seconds, repeating, ref);
  if (id == 0) {
    luaL_unref(state, LUA_REGISTRYINDEX, ref);
  }
  return id;
}

std::uint64_t luaAddEventSubscription(lua_State *state, LuaScriptHost &host,
                                      const std::string &eventName,
                                      const sol::function callback) {
  const int ref = callbackRef(state, callback);
  const std::uint64_t id = host.addEventSubscription(eventName, ref);
  if (id == 0) {
    luaL_unref(state, LUA_REGISTRYINDEX, ref);
  }
  return id;
}

int luaEmitEvent(lua_State *state, LuaScriptHost &host,
                 const std::string &eventName, const sol::object payload) {
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

sol::object luaReadSaveTable(lua_State *state, LuaScriptHost &host,
                             const std::string &slot) {
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
  if (!document.is_object() || !document.contains("state") ||
      !document["state"].is_object()) {
    return sol::nil;
  }

  int version = document.value("format_version", 1);
  sol::table stateTable =
      jsonToLuaObject(state, document["state"]).as<sol::table>();
  bool migrated = false;
  bool progressed = true;
  while (progressed) {
    progressed = false;
    for (const LuaScriptHost::SaveMigrationHook &hook :
         host.saveMigrationHooks()) {
      if (hook.fromVersion != version) {
        continue;
      }
      lua_rawgeti(state, LUA_REGISTRYINDEX, hook.callbackRef);
      stateTable.push();
      lua_pushinteger(state, hook.fromVersion);
      lua_pushinteger(state, hook.toVersion);
      std::string error;
      if (!luaCall(state, 3, 1, error)) {
        luaReportCallbackError(state, "Save.register_migration", {}, slot,
                               error);
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

bool luaWriteSaveTable(LuaScriptHost &host, const std::string &slot,
                       const sol::table table,
                       const sol::optional<int> version) {
  nlohmann::json state = luaObjectToJson(table);
  int formatVersion = version.value_or(2);
  if (state.contains("_format_version") &&
      state["_format_version"].is_number_integer()) {
    formatVersion = state["_format_version"].get<int>();
    state.erase("_format_version");
  }
  return host.writeSaveDocument(slot, state.dump(), formatVersion);
}

std::uint64_t luaRegisterSaveMigration(lua_State *state, LuaScriptHost &host,
                                       const int fromVersion,
                                       const int toVersion,
                                       const sol::function callback) {
  const int ref = callbackRef(state, callback);
  host.addSaveMigrationHook(fromVersion, toVersion, ref);
  return static_cast<std::uint64_t>(ref);
}

PhysicsContactFilter2D
luaContactFilterFromTable(const sol::optional<sol::table> filterTable) {
  PhysicsContactFilter2D filter;
  if (!filterTable.has_value()) {
    return filter;
  }

  const sol::table table = *filterTable;
  if (const sol::object layer = table["layer"]; layer.is<std::string>()) {
    filter.layer = layer.as<std::string>();
  }
  if (const sol::object normalXMin = table["normal_x_min"];
      normalXMin.is<float>()) {
    filter.normalXMin = normalXMin.as<float>();
  }
  if (const sol::object normalXMax = table["normal_x_max"];
      normalXMax.is<float>()) {
    filter.normalXMax = normalXMax.as<float>();
  }
  if (const sol::object normalYMin = table["normal_y_min"];
      normalYMin.is<float>()) {
    filter.normalYMin = normalYMin.as<float>();
  }
  if (const sol::object normalYMax = table["normal_y_max"];
      normalYMax.is<float>()) {
    filter.normalYMax = normalYMax.as<float>();
  }
  filter.includeTriggers = table.get_or("include_triggers", false);
  return filter;
}

sol::table luaContactsTable(lua_State *state,
                            const std::vector<PhysicsContact2D> &contacts) {
  sol::state_view lua(state);
  sol::table result = lua.create_table();
  int index = 1;
  for (const PhysicsContact2D &contact : contacts) {
    sol::table item = lua.create_table();
    item["entity_id"] = contact.entityId;
    item["other_entity_id"] = contact.otherEntityId;
    item["other_layer"] = contact.otherLayer;
    item["phase"] = contact.phase;
    item["point"] =
        sol::as_table(std::vector<float>{contact.point.x, contact.point.y});
    item["normal_x"] = contact.normal.x;
    item["normal_y"] = contact.normal.y;
    item["normal_impulse"] = contact.normalImpulse;
    item["is_trigger"] = contact.isTrigger;
    result[index++] = item;
  }
  return result;
}

} // namespace demi::runtime
