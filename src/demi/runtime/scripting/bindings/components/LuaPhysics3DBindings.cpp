#include "demi/runtime/scripting/bindings/components/LuaPhysics3DBindings.h"

#include "demi/runtime/scripting/LuaScriptHost.h"

#include <sol/sol.hpp>

namespace demi::runtime {

void LuaPhysics3DBindingModule::install(LuaScriptHost &host,
                                        lua_State *state) const {
  sol::table physics = sol::state_view(state).create_named_table("Physics3D");
  physics.set_function(
      "overlap_sphere",
      [&host](const float x, const float y, const float z, const float radius,
              const sol::optional<std::string> ignored) {
        return sol::as_table(
            host.physicsOverlapSphere3D(x, y, z, radius, ignored.value_or("")));
      });
  physics.set_function(
      "raycast",
      [state, &host](const float originX, const float originY,
                     const float originZ, const float directionX,
                     const float directionY, const float directionZ,
                     const float distance,
                     const sol::optional<std::string> ignored) -> sol::object {
        const auto hit = host.physicsRaycast3D(
            originX, originY, originZ, directionX, directionY, directionZ,
            distance, ignored.value_or(""));
        if (!hit)
          return sol::make_object(state, sol::nil);
        sol::table result = sol::state_view(state).create_table();
        result["entity_id"] = hit->entityId;
        result["point"] = sol::as_table(
            std::vector<float>{hit->point.x, hit->point.y, hit->point.z});
        result["normal"] = sol::as_table(
            std::vector<float>{hit->normal.x, hit->normal.y, hit->normal.z});
        result["distance"] = hit->distance;
        return sol::make_object(state, result);
      });
}

} // namespace demi::runtime
