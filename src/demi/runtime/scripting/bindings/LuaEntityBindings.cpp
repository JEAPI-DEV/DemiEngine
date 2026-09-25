#include "demi/runtime/scripting/bindings/LuaEntityBindings.h"

#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <sol/sol.hpp>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace demi::runtime {
namespace {

int spawnVectorDimensions(const sol::object &value) {
  if (value.get_type() != sol::type::table)
    return 0;
  int count = 0;
  int maximumIndex = 0;
  for (const auto &[key, coordinate] : value.as<sol::table>()) {
    if (key.get_type() != sol::type::number ||
        coordinate.get_type() != sol::type::number)
      return 0;
    const double index = key.as<double>();
    const double number = coordinate.as<double>();
    if ((index != 1 && index != 2 && index != 3) ||
        !std::isfinite(number) ||
        std::abs(number) > std::numeric_limits<float>::max())
      return 0;
    maximumIndex = std::max(maximumIndex, static_cast<int>(index));
    ++count;
  }
  return (count == 2 || count == 3) && maximumIndex == count ? count : 0;
}

} // namespace

void LuaEntityBindingModule::install(LuaScriptHost &host, lua_State *state) const {
  sol::state_view lua(state);
  sol::table entity = lua.create_named_table("Entity");
  entity.set_function("find", [&host](const std::string &idOrName) {
    return host.findEntityId(idOrName);
  });
  entity.set_function(
      "create", [&host](const std::string &entityId, const sol::table spec) {
        nlohmann::json json = luaObjectToJson(spec);
        json["id"] = entityId;
        std::string error;
        std::optional<Entity> created =
            RuntimeObjectModel::buildEntity(json, error);
        if (!created.has_value()) {
          std::cerr << "Entity.create failed for '" << entityId << "': "
                    << error << '\n';
          return false;
        }
        return host.createEntity(std::move(*created));
      });
  entity.set_function(
      "replace", [&host](const std::string &entityId, const sol::table spec) {
        nlohmann::json json = luaObjectToJson(spec);
        json["id"] = entityId;
        std::string error;
        std::optional<Entity> replacement =
            RuntimeObjectModel::buildEntity(json, error);
        return replacement.has_value() &&
               host.replaceEntity(std::move(*replacement));
      });
  entity.set_function("exists", [&host](const std::string &entityId) {
    return host.entityExists(entityId);
  });
  entity.set_function("clone", [&host](const std::string &sourceId,
                                       const std::string &newId) {
    return host.cloneEntity(sourceId, newId);
  });
  entity.set_function("destroy", [&host](const std::string &entityId) {
    return host.destroyEntity(entityId);
  });
  entity.set_function(
      "spawn", [&host](const std::string &entityId, const sol::table options)
          -> std::tuple<bool, std::optional<std::string>> {
        const auto failure = [](std::string error) {
          return std::tuple{false, std::optional<std::string>{
                                       "Entity.spawn: " + std::move(error)}};
        };
        for (const auto &[key, guidance] :
             {std::pair{"prefab", "use Prefab.instantiate instead"},
              std::pair{"ttl", "use Timer.after for lifetime handling"}}) {
          const sol::object value = options[key];
          if (value.valid() && value != sol::nil)
            return failure(std::string(key) + " is unsupported; " + guidance);
        }
        int dimensions = 0;
        for (const char *key : {"position", "velocity"}) {
          const sol::object value = options[key];
          if (!value.valid() || value == sol::nil)
            continue;
          const int size = spawnVectorDimensions(value);
          if (size == 0)
            return failure(std::string(key) +
                           " must be a dense numeric array of 2 or 3 finite "
                           "coordinates representable as floats");
          if (dimensions != 0 && dimensions != size)
            return failure("position and velocity must have matching dimensions");
          dimensions = size;
        }
        try {
          nlohmann::json json = luaObjectToJson(options);
          if (!json.is_object())
            return failure("options must be an entity specification table");
          json["id"] = entityId;
          if (!json.contains("components"))
            json["components"] = nlohmann::json::object();
          if (!json["components"].is_object())
            return failure("components must be a table of component blocks");
          for (const char *key : {"position", "velocity"}) {
            const auto value = json.find(key);
            if (value == json.end())
              continue;
            const bool position = std::string_view(key) == "position";
            const std::string component =
                std::string(position ? "Transform" : "Rigidbody") +
                (dimensions == 2 ? "2D" : "3D");
            auto &components = json["components"];
            if (!components.contains(component))
              components[component] = nlohmann::json::object();
            auto &fields = components[component];
            if (!fields.is_object())
              return failure(component + " must be a component table");
            // Explicit component fields take precedence over valid shorthand.
            if (!fields.contains(key))
              fields[key] = *value;
            json.erase(value);
          }
          std::string error;
          auto created = RuntimeObjectModel::buildEntity(json, error);
          if (!created)
            return failure(error);
          if (!host.createEntity(std::move(*created)))
            return failure("creation rejected (entity ID already exists or is "
                           "pending, or the world is unavailable)");
          return {true, std::nullopt};
        } catch (const nlohmann::json::exception &error) {
          return failure(error.what());
        }
      });
  entity.set_function("destroy_many", [&host](const sol::table entityIds) {
    std::vector<std::string> ids;
    ids.reserve(entityIds.size());
    for (const auto &pair : entityIds) {
      if (pair.second.is<std::string>()) {
        ids.push_back(pair.second.as<std::string>());
      }
    }
    return host.destroyEntities(ids);
  });
  entity.set_function("set_enabled", [&host](const std::string &entityId,
                                             const bool enabled) {
    return host.setEntityEnabled(entityId, enabled);
  });
  entity.set_function("is_enabled", [&host](const std::string &entityId) {
    return host.isEntityEnabled(entityId);
  });
  entity.set_function("add_component", [&host](const std::string &entityId,
                                               const std::string &component,
                                               const sol::table values) {
    return host.addEntityComponent(entityId, component,
                                   luaObjectToJson(values));
  });
  entity.set_function("remove_component", [&host](
                                                const std::string &entityId,
                                                const std::string &component) {
    return host.removeEntityComponent(entityId, component);
  });
  entity.set_function("has_component", [&host](const std::string &entityId,
                                               const std::string &component) {
    return host.hasEntityComponent(entityId, component);
  });
  entity.set_function("get_config", [state, &host](const std::string &entityId,
                                            const std::string &component,
                                            const std::string &field) {
    const auto value =
        host.entityComponentField(entityId, component, field);
    return value.has_value() ? jsonToLuaObject(state, *value)
                             : sol::make_object(state, sol::nil);
  });
  entity.set_function("set_field", [&host](const std::string &entityId,
                                     const std::string &component,
                                     const std::string &field,
                                     const sol::object value) {
    return host.setEntityComponentField(entityId, component, field,
                                        luaObjectToJson(value));
  });
  entity.set_function("query", [&host](const sol::table queryTable) {
    EntityQuery query;
    const auto readStrings = [](const sol::object &object) {
      std::vector<std::string> values;
      if (!object.is<sol::table>())
        return values;
      for (const auto &entry : object.as<sol::table>()) {
        if (entry.second.is<std::string>())
          values.push_back(entry.second.as<std::string>());
      }
      return values;
    };
    query.allComponents = readStrings(queryTable["all"]);
    query.tags = readStrings(queryTable["tags"]);
    const sol::object layer = queryTable["layer"];
    if (layer.is<std::string>())
      query.layer = layer.as<std::string>();
    query.includeDisabled = queryTable.get_or("include_disabled", false);
    return host.queryEntities(query);
  });
  entity.set_function("set_parent", [&host](
                                           const std::string &entityId,
                                           const sol::object parent) {
    std::optional<std::string> parentId;
    if (parent.valid() && parent != sol::nil && parent.is<std::string>())
      parentId = parent.as<std::string>();
    return host.setEntityParent(entityId, parentId);
  });
  entity.set_function("parent", [&host](const std::string &entityId) {
    return host.entityParent(entityId);
  });
  entity.set_function("children", [&host](const std::string &entityId) {
    return host.entityChildren(entityId);
  });
  entity.set_function("local_position",
                      [state, &host](const std::string &entityId) {
                        const auto value =
                            host.entityLocalPosition(entityId);
                        return value.has_value()
                                   ? jsonToLuaObject(state, *value)
                                   : sol::make_object(state, sol::nil);
                      });
  entity.set_function("world_position",
                      [state, &host](const std::string &entityId) {
                        const auto value =
                            host.entityWorldPosition(entityId);
                        return value.has_value()
                                   ? jsonToLuaObject(state, *value)
                                   : sol::make_object(state, sol::nil);
                      });
}

} // namespace demi::runtime
