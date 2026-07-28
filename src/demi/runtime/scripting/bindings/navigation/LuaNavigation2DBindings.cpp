#include "demi/runtime/scripting/bindings/navigation/LuaNavigation2DBindings.h"

#include "demi/runtime/scripting/LuaScriptHost.h"

#include <sol/sol.hpp>

namespace demi::runtime {

void LuaNavigation2DBindingModule::install(LuaScriptHost &host,
                                           lua_State *state) const {
  auto &grid = host.navigationGrid2D();
  sol::state_view lua(state);
  sol::table navigation = lua.create_named_table("Navigation2D");
  navigation.set_function(
      "configure",
      [&grid](const int width, const int height, const float cellSize,
              sol::optional<float> originX, sol::optional<float> originY) {
        return grid.configure(width, height, cellSize,
                              {originX.value_or(0.0F), originY.value_or(0.0F)});
      });
  navigation.set_function("clear", [&grid] { grid.clear(); });
  navigation.set_function("available", [&grid] { return grid.available(); });
  navigation.set_function(
      "set_blocked", [&grid](const int x, const int y, const bool blocked) {
        return grid.setBlocked({x, y}, blocked);
      });
  navigation.set_function("set_cost",
                          [&grid](const int x, const int y, const float cost) {
                            return grid.setCost({x, y}, cost);
                          });
  navigation.set_function("path", [state,
                                   &grid](const int startX, const int startY,
                                          const int goalX, const int goalY,
                                          sol::optional<bool> diagonal) {
    const auto result =
        grid.path({startX, startY}, {goalX, goalY}, diagonal.value_or(false));
    sol::table path = sol::state_view(state).create_table();
    int index = 1;
    for (const auto cell : result.cells) {
      sol::table point = sol::state_view(state).create_table();
      point[1] = cell.x;
      point[2] = cell.y;
      if (const auto world = grid.cellToWorld(cell)) {
        point["world_x"] = world->x;
        point["world_y"] = world->y;
      }
      path[index++] = point;
    }
    return std::tuple{path, result.diagnostic};
  });
  navigation.set_function(
      "world_to_cell", [state, &grid](const float x, const float y) {
        const auto cell = grid.worldToCell({x, y});
        if (!cell)
          return std::tuple{sol::make_object(state, sol::nil),
                            sol::make_object(state, sol::nil)};
        return std::tuple{sol::make_object(state, cell->x),
                          sol::make_object(state, cell->y)};
      });
  navigation.set_function(
      "cell_to_world", [state, &grid](const int x, const int y) {
        const auto world = grid.cellToWorld({x, y});
        if (!world)
          return std::tuple{sol::make_object(state, sol::nil),
                            sol::make_object(state, sol::nil)};
        return std::tuple{sol::make_object(state, world->x),
                          sol::make_object(state, world->y)};
      });
}

} // namespace demi::runtime
