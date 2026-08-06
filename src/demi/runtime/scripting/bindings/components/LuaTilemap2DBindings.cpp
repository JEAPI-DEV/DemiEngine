#include "demi/runtime/scripting/bindings/components/LuaTilemap2DBindings.h"

#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <sol/sol.hpp>

namespace demi::runtime {

void LuaTilemap2DBindingModule::install(LuaScriptHost &host,
                                        lua_State *state) const {
  sol::table tilemap = sol::state_view(state).create_named_table("Tilemap2D");
  tilemap.set_function(
      "get_tile",
      [state, &host](const std::string &entityId, const std::string &layer,
                     const int column, const int row) -> sol::object {
        const auto value =
            host.tilemapRuntime().tile(entityId, layer, column, row);
        return value ? sol::make_object(state, *value)
                     : sol::make_object(state, sol::nil);
      });
  tilemap.set_function("set_tile", [&host](const std::string &entityId,
                                           const std::string &layer,
                                           const int column, const int row,
                                           const int value) {
    return host.tilemapRuntime().setTile(entityId, layer, column, row, value);
  });
  tilemap.set_function("clear_overrides", [&host](const std::string &entityId) {
    return host.tilemapRuntime().clearOverrides(entityId);
  });
  tilemap.set_function("bake_navigation", [&host](const std::string &entityId) {
    return host.tilemapRuntime().bakeNavigation(entityId);
  });
  tilemap.set_function("objects", [state, &host](const std::string &entityId,
                                                 const std::string &layerName) {
    sol::state_view lua(state);
    sol::table result = lua.create_table();
    int index = 1;
    for (const TilemapObject2D &object :
         host.tilemapRuntime().objects(entityId, layerName)) {
      sol::table value = lua.create_table();
      value["id"] = object.id;
      value["type"] = object.type;
      value["x"] = object.position.x;
      value["y"] = object.position.y;
      value["width"] = object.size.x;
      value["height"] = object.size.y;
      value["properties"] = jsonToLuaObject(state, object.properties);
      result[index++] = value;
    }
    return result;
  });
}

} // namespace demi::runtime
