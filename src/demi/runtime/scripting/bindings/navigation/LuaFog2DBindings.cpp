#include "demi/runtime/scripting/bindings/navigation/LuaFog2DBindings.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include <sol/sol.hpp>

namespace demi::runtime {
void LuaFog2DBindingModule::install(LuaScriptHost &host,
                                    lua_State *state) const {
  sol::state_view lua(state);
  sol::table service = lua.create_named_table("Fog2D");
  service.set_function("configure", [&host](int width, int height,
                                            float cellSize, float x, float y,
                                            float r, float g, float b,
                                            float memoryOpacity) {
    return host.configureFog2D(width, height, cellSize, {x, y}, {r, g, b, 1.0F},
                               memoryOpacity);
  });
  service.set_function("reveal",
                       [&host](int x, int y) { host.revealFog2D(x, y); });
  service.set_function("clear", [&host]() { host.clearFog2D(); });
  service.set_function("update", [&host](float x, float y, float radius,
                                         float feather,
                                         const std::string &layer) {
    return host.updateFog2D({x, y}, radius, feather, layer);
  });
}
} // namespace demi::runtime
