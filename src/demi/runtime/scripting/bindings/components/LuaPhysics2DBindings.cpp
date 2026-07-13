#include "demi/runtime/scripting/bindings/components/LuaPhysics2DBindings.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include <sol/sol.hpp>
namespace demi::runtime {
void LuaPhysics2DBindingModule::install(LuaScriptHost &host,
                                        lua_State *state) const {
  sol::table physics = sol::state_view(state).create_named_table("Physics2D");
  physics.set_function("overlap_box",
                       [&host](float x, float y, float width, float height,
                               sol::optional<std::string> ignored) {
                         return host.physicsOverlapBox(x, y, width, height,
                                                       ignored.value_or(""));
                       });
  physics.set_function(
      "overlap_circle",
      [&host](float x, float y, float radius, sol::optional<std::string> layer,
              sol::optional<std::string> ignored) {
        return sol::as_table(host.physicsOverlapCircle(
            x, y, radius, layer.value_or(""), ignored.value_or("")));
      });
  physics.set_function(
      "raycast",
      [state, &host](float originX, float originY, float directionX,
                     float directionY, float distance,
                     sol::optional<std::string> layer,
                     sol::optional<std::string> ignored) -> sol::object {
        const auto hit = host.physicsRaycast(
            originX, originY, directionX, directionY, distance,
            layer.value_or(""), ignored.value_or(""));
        if (!hit)
          return sol::make_object(state, sol::nil);
        sol::table result = sol::state_view(state).create_table();
        result["entity_id"] = hit->entityId;
        result["point"] =
            sol::as_table(std::vector<float>{hit->point.x, hit->point.y});
        result["normal"] =
            sol::as_table(std::vector<float>{hit->normal.x, hit->normal.y});
        result["distance"] = hit->distance;
        return sol::make_object(state, result);
      });
  physics.set_function(
      "has_contact",
      [&host](const std::string &id, sol::optional<sol::table> filter) {
        return host.physicsHasContact(id, luaContactFilterFromTable(filter));
      });
  physics.set_function("contacts", [state, &host](const std::string &id) {
    return luaContactsTable(state, host.physicsContacts(id));
  });
}
} // namespace demi::runtime
