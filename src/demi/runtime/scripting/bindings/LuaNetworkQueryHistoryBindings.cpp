#include "demi/runtime/scripting/bindings/LuaNetworkQueryHistoryBindings.h"

#include "demi/runtime/network/GameNetworkSession.h"
#include "demi/runtime/network/NetworkQueryHistory2D.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include <sol/sol.hpp>

#include <string>
#include <vector>

namespace demi::runtime {

void installNetworkQueryHistoryBindings(LuaScriptHost &host, lua_State *state,
                                        GameNetworkSession &session,
                                        NetworkQueryHistory2D &history) {
  sol::state_view lua(state);
  sol::table networkSession = lua["NetworkSession"];
  networkSession.set_function(
      "record_query_snapshot",
      [&host, &session, &history](const std::uint64_t serverTick,
                                  const sol::table circles) {
        if (!host.networkIsHost() || host.networkContract() == nullptr) {
          session.reject("only the authoritative host with a contract may "
                         "record network query history");
          return false;
        }
        std::vector<NetworkHistoricalCircle2D> snapshot;
        snapshot.reserve(circles.size());
        for (const auto &[unused, value] : circles) {
          (void)unused;
          if (!value.is<sol::table>()) {
            session.reject("query history circles must be tables");
            return false;
          }
          const sol::table circle = value.as<sol::table>();
          snapshot.push_back({
              .entityId = circle.get_or("entity_id", std::string{}),
              .layer = circle.get_or("layer", std::string{}),
              .x = circle.get_or("x", 0.0),
              .y = circle.get_or("y", 0.0),
              .radius = circle.get_or("radius", 0.0),
          });
        }
        if (!history.record(serverTick, std::move(snapshot))) {
          session.reject("network query snapshot is invalid, stale, or exceeds "
                         "its configured bounds");
          return false;
        }
        return true;
      });
  networkSession.set_function(
      "historical_raycast",
      [state, &host, &session,
       &history](const std::uint64_t requestedTick, const double originX,
                 const double originY, const double directionX,
                 const double directionY, const double maximumDistance,
                 const sol::optional<std::string> layer,
                 const sol::optional<std::string> ignoredEntityId) {
        if (!host.networkIsHost() || host.networkContract() == nullptr) {
          session.reject("only the authoritative host with a contract may "
                         "query network history");
          return sol::make_object(state, sol::nil);
        }
        const auto hit = history.raycast(
            requestedTick, originX, originY, directionX, directionY,
            maximumDistance, layer.value_or(""), ignoredEntityId.value_or(""));
        if (!hit)
          return sol::make_object(state, sol::nil);
        sol::state_view lua(state);
        sol::table result = lua.create_table();
        result["entity_id"] = hit->entityId;
        result["layer"] = hit->layer;
        result["sampled_tick"] = hit->sampledTick;
        result["x"] = hit->x;
        result["y"] = hit->y;
        result["normal_x"] = hit->normalX;
        result["normal_y"] = hit->normalY;
        result["distance"] = hit->distance;
        return sol::make_object(state, result);
      });
  networkSession.set_function("query_history_diagnostics", [state, &history] {
    sol::state_view lua(state);
    sol::table result = lua.create_table();
    const auto &counters = history.counters();
    result["depth"] = history.depth();
    result["latest_tick"] = history.latestTick();
    result["recorded"] = counters.recorded;
    result["rejected"] = counters.rejected;
    result["dropped"] = counters.dropped;
    result["queries"] = counters.queries;
    result["clamped_queries"] = counters.clampedQueries;
    result["misses"] = counters.misses;
    return result;
  });
  networkSession.set_function(
      "clear_query_history", [&host, &session, &history] {
        if (!host.networkIsHost()) {
          session.reject("only the authoritative host may clear "
                         "network query history");
          return false;
        }
        history.clear();
        return true;
      });
}

} // namespace demi::runtime
