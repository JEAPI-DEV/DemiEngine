#include "demi/runtime/scripting/bindings/scene/LuaPrefabBindings.h"

#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"
#include <cmath>
#include <limits>
#include <sol/sol.hpp>

namespace demi::runtime {

void LuaPrefabBindingModule::install(LuaScriptHost &host, lua_State *state) const {
  sol::state_view lua(state);
  sol::table prefab = lua.create_named_table("Prefab");
  prefab.set_function("placements", [&host, state](sol::optional<std::string> ancestor) {
    return jsonToLuaObject(state, host.prefabPlacements(ancestor.value_or("")));
  });
  prefab.set_function(
      "instantiate",
      [&host](const std::string &prefabId, const sol::table optionsTable) {
        PrefabInstantiateOptions options;
        options.id = optionsTable.get_or("id", std::string{});
        options.pooled = optionsTable.get_or("pooled", false);
        const sol::object position = optionsTable["position"];
        if (position.is<sol::table>()) {
          const sol::table value = position.as<sol::table>();
          options.position = Vec3{.x = value.get_or(1, 0.0F),
                                  .y = value.get_or(2, 0.0F),
                                  .z = value.get_or(3, 0.0F)};
        }
        const sol::object overrides = optionsTable["overrides"];
        if (overrides.is<sol::table>())
          options.overrides = luaObjectToJson(overrides);
        return host.instantiatePrefab(prefabId, options);
      });
  prefab.set_function("release", [&host](const std::string &instanceId) {
    return host.releasePrefab(instanceId);
  });
  prefab.set_function("pooled_count", [&host](const std::string &prefabId) {
    return host.pooledPrefabCount(prefabId);
  });
  prefab.set_function("set_template_cache_capacity", [&host](double entries) {
    if (!std::isfinite(entries) || entries < 0 || std::floor(entries) != entries ||
        entries >= static_cast<double>(std::numeric_limits<std::size_t>::max())) {
      return false;
    }
    host.setPrefabTemplateCacheCapacity(static_cast<std::size_t>(entries));
    return true;
  });

}

} // namespace demi::runtime
