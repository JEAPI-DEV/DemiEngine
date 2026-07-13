#include "demi/runtime/scripting/bindings/isometric/LuaIsoGridBindings.h"

#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

#include <sol/sol.hpp>

#include <tuple>

namespace demi::runtime {

namespace {

sol::object pathTable(lua_State *state, const isometric::PathResult &result) {
  if (!result.success)
    return sol::make_object(state, sol::nil);
  sol::table path = sol::state_view(state).create_table();
  int index = 1;
  for (const isometric::GridCell cell : result.cells) {
    sol::table point = sol::state_view(state).create_table();
    point[1] = cell.x;
    point[2] = cell.y;
    path[index++] = point;
  }
  return sol::make_object(state, path);
}

} // namespace

void LuaIsoGridBindingModule::install(LuaScriptHost &host,
                                      lua_State *state) const {
  auto &api = host.isoGridApi();
  sol::table grid = sol::state_view(state).create_named_table("Grid");
  grid.set_function("available", [&api] { return api.available(); });
  grid.set_function("tile_to_world", [state,
                                      &api](const float x, const float y,
                                            sol::optional<float> elevation) {
    return luaVec2Result(state, api.tileToWorld({x, y}, elevation.value_or(0)));
  });
  grid.set_function("world_to_tile",
                    [state, &api](const float x, const float y) {
                      return luaVec2Result(state, api.worldToTile({x, y}));
                    });
  grid.set_function("get_tile", [state, &api](const std::string &id) {
    return luaVec2Result(state, api.entityTile(id));
  });
  grid.set_function("set_tile", [&api](const std::string &id, const float x,
                                       const float y,
                                       sol::optional<float> elevation) {
    return api.setEntityTile(id, {x, y}, elevation.value_or(0.0F));
  });
  grid.set_function("entity_at", [&api](const int x, const int y) {
    return api.entityAt({x, y});
  });
  grid.set_function("can_place", [&api](const int x, const int y,
                                        const int width, const int height) {
    const auto result = api.canPlace({x, y}, {width, height});
    return std::tuple{result.allowed, result.code, result.message};
  });
  grid.set_function("path", [state, &api](const int startX, const int startY,
                                          const int goalX, const int goalY) {
    const auto result = api.path({startX, startY}, {goalX, goalY});
    return std::tuple{pathTable(state, result), result.diagnostic};
  });
  grid.set_function(
      "path_with_blocker",
      [state, &api](const int startX, const int startY, const int goalX,
                    const int goalY, const int blockX, const int blockY,
                    const int blockWidth, const int blockHeight) {
        const auto result =
            api.path({startX, startY}, {goalX, goalY},
                     isometric::PlacementRequest{
                         .origin = {blockX, blockY},
                         .footprint = {blockWidth, blockHeight}});
        return std::tuple{pathTable(state, result), result.diagnostic};
      });
  grid.set_function("set_preview",
                    [&api](const int x, const int y, const int width,
                           const int height, const bool valid) {
                      api.setPreview({x, y}, {width, height}, valid);
                    });
  grid.set_function("clear_preview", [&api] { api.clearPreview(); });
}

} // namespace demi::runtime
