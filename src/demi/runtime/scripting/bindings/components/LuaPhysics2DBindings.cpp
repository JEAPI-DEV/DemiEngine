#include "demi/runtime/scripting/bindings/components/LuaPhysics2DBindings.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include <sol/sol.hpp>
namespace demi::runtime {
namespace {
sol::table queryHitTable(lua_State *state, const PhysicsQueryHit2D &hit) {
  sol::table item = sol::state_view(state).create_table();
  item["entity_id"] = hit.entityId;
  item["layer"] = hit.layer;
  item["point"] = sol::as_table(std::vector<float>{hit.point.x, hit.point.y});
  item["normal"] =
      sol::as_table(std::vector<float>{hit.normal.x, hit.normal.y});
  item["distance"] = hit.distance;
  item["fraction"] = hit.fraction;
  return item;
}

sol::table queryHitsTable(lua_State *state,
                          const std::vector<PhysicsQueryHit2D> &hits) {
  sol::table result = sol::state_view(state).create_table();
  int index = 1;
  for (const auto &hit : hits) {
    result[index++] = queryHitTable(state, hit);
  }
  return result;
}

std::optional<std::vector<PhysicsRay2D>> readRays(const sol::table &requests) {
  const std::size_t count = requests.size();
  if (count > 4096) {
    return std::nullopt;
  }
  for (const auto &pair : requests) {
    if (!pair.first.is<int>()) {
      return std::nullopt;
    }
    const int index = pair.first.as<int>();
    if (index < 1 || static_cast<std::size_t>(index) > count) {
      return std::nullopt;
    }
  }
  std::vector<PhysicsRay2D> rays;
  rays.reserve(count);
  for (std::size_t index = 1; index <= count; ++index) {
    const sol::object value = requests[index];
    if (!value.is<sol::table>()) {
      return std::nullopt;
    }
    const sol::table ray = value.as<sol::table>();
    const sol::optional<float> dx = ray[1];
    const sol::optional<float> dy = ray[2];
    const sol::optional<float> distance = ray[3];
    if (!dx || !dy || !distance || ray.size() != 3) {
      return std::nullopt;
    }
    rays.push_back({{*dx, *dy}, *distance});
  }
  return rays;
}
} // namespace

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
      "overlap_box_all",
      [state, &host](float x, float y, float width, float height,
                     sol::optional<std::string> layer,
                     sol::optional<std::string> ignored) {
        return queryHitsTable(state,
                              host.physicsOverlapBoxAll(x, y, width, height,
                                                        layer.value_or(""),
                                                        ignored.value_or("")));
      });
  physics.set_function(
      "overlap_circle_all", [state, &host](float x, float y, float radius,
                                           sol::optional<std::string> layer,
                                           sol::optional<std::string> ignored) {
        return queryHitsTable(
            state, host.physicsOverlapCircleAll(
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
        return sol::make_object(state, queryHitTable(state, *hit));
      });
  physics.set_function(
      "raycast_batch",
      [state, &host](float x, float y, const sol::table &requests,
                     sol::optional<std::string> layer,
                     sol::optional<std::string> ignored)
          -> std::tuple<sol::object, sol::object> {
        const auto rays = readRays(requests);
        if (!rays) {
          return {sol::make_object(state, sol::nil),
                  sol::make_object(
                      state, "Expected a dense array of at most 4096 "
                             "{direction_x, direction_y, distance} rays.")};
        }
        const auto batch = host.physicsRaycastBatch(
            {x, y}, *rays, layer.value_or(""), ignored.value_or(""));
        if (!batch.error.empty()) {
          return {sol::make_object(state, sol::nil),
                  sol::make_object(state, batch.error)};
        }
        sol::table result = sol::state_view(state).create_table();
        int index = 1;
        for (const auto &hit : batch.hits) {
          result[index++] =
              hit ? sol::make_object(state, queryHitTable(state, *hit))
                  : sol::make_object(state, false);
        }
        return {sol::make_object(state, result),
                sol::make_object(state, sol::nil)};
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
