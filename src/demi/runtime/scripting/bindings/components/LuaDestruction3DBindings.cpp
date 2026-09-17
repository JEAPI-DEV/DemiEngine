#include "demi/runtime/scripting/bindings/components/LuaDestruction3DBindings.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include <sol/sol.hpp>
namespace demi::runtime {
void LuaDestruction3DBindingModule::install(LuaScriptHost &host,
                                            lua_State *state) const {
  auto api = sol::state_view(state).create_named_table("Destruction3D");
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
}
} // namespace demi::runtime
