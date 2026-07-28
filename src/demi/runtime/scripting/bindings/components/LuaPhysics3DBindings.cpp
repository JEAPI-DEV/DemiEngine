#include "demi/runtime/scripting/bindings/components/LuaPhysics3DBindings.h"

#include "demi/runtime/scripting/LuaScriptHost.h"

#include <sol/sol.hpp>

namespace demi::runtime {
namespace {

sol::table hitTable(lua_State *state, const PhysicsQueryHit3D &hit) {
  sol::table result = sol::state_view(state).create_table();
  result["entity_id"] = hit.entityId;
  result["layer"] = hit.layer;
  result["point"] =
      sol::as_table(std::vector<float>{hit.point.x, hit.point.y, hit.point.z});
  result["normal"] = sol::as_table(
      std::vector<float>{hit.normal.x, hit.normal.y, hit.normal.z});
  result["distance"] = hit.distance;
  result["fraction"] = hit.fraction;
  result["is_trigger"] = hit.isTrigger;
  return result;
}

sol::table hitList(lua_State *state,
                   const std::vector<PhysicsQueryHit3D> &hits) {
  sol::table result = sol::state_view(state).create_table();
  int index = 1;
  for (const PhysicsQueryHit3D &hit : hits)
    result[index++] = hitTable(state, hit);
  return result;
}

} // namespace

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
      "overlap_sphere_all",
      [state, &host](const float x, const float y, const float z,
                     const float radius, const sol::optional<std::string> layer,
                     const sol::optional<std::string> ignored) {
        return hitList(state, host.physicsOverlapSphereAll3D(
                                  x, y, z, radius, layer.value_or(""),
                                  ignored.value_or("")));
      });
  physics.set_function(
      "overlap_box_all",
      [state, &host](const float x, const float y, const float z,
                     const float width, const float height, const float depth,
                     const sol::optional<std::string> layer,
                     const sol::optional<std::string> ignored) {
        return hitList(state, host.physicsOverlapBoxAll3D(
                                  x, y, z, width, height, depth,
                                  layer.value_or(""), ignored.value_or("")));
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
        return sol::make_object(state, hitTable(state, *hit));
      });
  physics.set_function(
      "sphere_cast",
      [state, &host](const float originX, const float originY,
                     const float originZ, const float radius,
                     const float directionX, const float directionY,
                     const float directionZ, const float distance,
                     const sol::optional<std::string> layer,
                     const sol::optional<std::string> ignored) -> sol::object {
        const auto hit = host.physicsSphereCast3D(
            originX, originY, originZ, radius, directionX, directionY,
            directionZ, distance, layer.value_or(""), ignored.value_or(""));
        return hit ? sol::make_object(state, hitTable(state, *hit))
                   : sol::make_object(state, sol::nil);
      });
}

} // namespace demi::runtime
