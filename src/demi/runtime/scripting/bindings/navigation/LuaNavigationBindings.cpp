#include "demi/runtime/scripting/bindings/navigation/LuaNavigationBindings.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include <memory>
#include <sol/sol.hpp>

namespace demi::runtime {
void LuaNavigationBindingModule::install(LuaScriptHost &host,
                                         lua_State *state) const {
  using Grid = navigation::NavigationGrid2D;
  sol::state_view lua(state);
  lua.new_usertype<Grid>(
      "NavigationGrid", sol::no_constructor, "configure",
      [](Grid &grid, int width, int height, float size, sol::optional<float> x,
         sol::optional<float> y) {
        return grid.configure(width, height, size,
                              {x.value_or(0), y.value_or(0)});
      },
      "clear", &Grid::clear, "available", &Grid::available, "set_blocked",
      [](Grid &grid, int x, int y, bool blocked) {
        return grid.setBlocked({x, y}, blocked);
      },
      "set_cost",
      [](Grid &grid, int x, int y, float cost) {
        return grid.setCost({x, y}, cost);
      },
      "blocked",
      [](const Grid &grid, int x, int y) { return grid.blocked({x, y}); },
      "path",
      [state](const Grid &grid, int sx, int sy, int gx, int gy,
              sol::optional<bool> diagonal) {
        const auto result =
            grid.path({sx, sy}, {gx, gy}, diagonal.value_or(false));
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
      },
      "world_to_cell",
      [state](const Grid &grid, float x, float y) {
        const auto cell = grid.worldToCell({x, y});
        return std::tuple{cell ? sol::make_object(state, cell->x)
                               : sol::make_object(state, sol::nil),
                          cell ? sol::make_object(state, cell->y)
                               : sol::make_object(state, sol::nil)};
      },
      "cell_to_world",
      [state](const Grid &grid, int x, int y) {
        const auto world = grid.cellToWorld({x, y});
        return std::tuple{world ? sol::make_object(state, world->x)
                                : sol::make_object(state, sol::nil),
                          world ? sol::make_object(state, world->y)
                                : sol::make_object(state, sol::nil)};
      });
  sol::table service = lua.create_named_table("Navigation");
  service.set_function("create_grid",
                       [state](int width, int height, float size,
                               sol::optional<float> x,
                               sol::optional<float> y) -> sol::object {
                         auto grid = std::make_shared<Grid>();
                         if (!grid->configure(width, height, size,
                                              {x.value_or(0), y.value_or(0)}))
                           return sol::make_object(state, sol::nil);
                         return sol::make_object(state, std::move(grid));
                       });
  service.set_function("default_grid",
                       [&host]() -> Grid & { return host.navigationGrid2D(); });
  lua["NavigationGrid"] = sol::nil;
}
} // namespace demi::runtime
