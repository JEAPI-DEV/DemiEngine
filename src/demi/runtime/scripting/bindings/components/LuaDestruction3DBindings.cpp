#include "demi/runtime/scripting/bindings/components/LuaDestruction3DBindings.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"
#include <sol/sol.hpp>
#include <stdexcept>
namespace demi::runtime {
namespace {
DestructionImpact3D readImpact(const sol::table &options) {
  for (const auto &entry : options) {
    if (entry.first.get_type() != sol::type::string)
      throw std::invalid_argument("Impact option keys must be strings");
    const auto name = entry.first.as<std::string>();
    if (name != "position" && name != "radius" && name != "energy" &&
        name != "impulse" && name != "direction" && name != "entity")
      throw std::invalid_argument("Unknown impact option: " + name);
  }
  const auto number = [&](const char *name, float fallback) {
    const sol::object value = options[name];
    if (!value.valid() || value == sol::nil)
      return fallback;
    if (value.get_type() != sol::type::number)
      throw std::invalid_argument(std::string("Impact ") + name +
                                  " must be a number");
    return float(value.as<double>());
  };
  const auto vector = [&](const char *name, bool required) {
    const sol::object value = options[name];
    if (!required && (!value.valid() || value == sol::nil))
      return Vec3{};
    if (!value.is<sol::table>())
      throw std::invalid_argument(std::string("Impact ") + name +
                                  " requires [x,y,z]");
    const auto table = value.as<sol::table>();
    if (table.size() != 3)
      throw std::invalid_argument("Impact vectors require three numbers");
    Vec3 result;
    float *axes[] = {&result.x, &result.y, &result.z};
    for (int i = 0; i < 3; ++i) {
      sol::object axis = table[i + 1];
      if (axis.get_type() != sol::type::number)
        throw std::invalid_argument("Impact vectors require three numbers");
      *axes[i] = float(axis.as<double>());
    }
    return result;
  };
  DestructionImpact3D hit;
  hit.position = vector("position", true);
  hit.direction = vector("direction", false);
  hit.radius = number("radius", hit.radius);
  hit.energy = number("energy", 0);
  hit.impulse = number("impulse", 0);
  const sol::object entity = options["entity"];
  if (entity.valid() && entity != sol::nil) {
    if (entity.get_type() != sol::type::string)
      throw std::invalid_argument("Impact entity must be a string");
    hit.entity = entity.as<std::string>();
  }
  return hit;
}
} // namespace
void LuaDestruction3DBindingModule::install(LuaScriptHost &host,
                                            lua_State *state) const {
  auto api = sol::state_view(state).create_named_table("Destruction3D");
  api.set_function("impact", [&host](const sol::table &options) {
    std::string error;
    std::size_t affected = 0;
    bool accepted = false;
    try {
      accepted =
          host.applyDestructionImpact3D(readImpact(options), affected, error);
    } catch (const std::exception &exception) {
      error = exception.what();
    }
    return std::tuple{accepted, error, affected};
  });
  api.set_function("damage_part", [&host](const std::string &entity,
                                          const std::string &part,
                                          float damage) {
    std::string error;
    const bool accepted =
        host.damageDestructiblePart3D(entity, part, damage, error);
    return std::tuple{accepted, error};
  });
  api.set_function("state", [&host, state](const std::string &entity) {
    const auto value = host.destructionState3D(entity);
    auto result = sol::state_view(state).create_table();
    result["root"] = value.root;
    result["status"] = value.status;
    result["error"] = value.error;
    result["revision"] = value.revision;
    result["bodies"] = value.bodies;
    result["parts"] = sol::as_table(value.parts);
    return result;
  });
  api.set_function("checkpoint", [&host,state](const std::string &entity) {
    std::string error;
    const auto value=host.destructionCheckpoint3D(entity,error);
    return std::tuple{jsonToLuaObject(state,value),error};
  });
  api.set_function("restore", [&host](const std::string &entity,sol::table value) {
    std::string error;
    const bool ok=host.restoreDestruction3D(entity,luaObjectToJson(value),error);
    return std::tuple{ok,error};
  });
  api.set_function("retire_debris", [&host](const std::string &entity) {
    std::string error;
    const bool ok=host.retireDestructionDebris3D(entity,error);
    return std::tuple{ok,error};
  });
}
} // namespace demi::runtime
