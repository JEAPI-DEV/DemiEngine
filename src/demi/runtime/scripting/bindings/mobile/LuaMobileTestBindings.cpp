#include "demi/runtime/scripting/bindings/mobile/LuaMobileTestBindings.h"

#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/ui/UiModel.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <sol/sol.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace demi::runtime {

void LuaMobileTestBindingModule::install(LuaScriptHost &host,
                                         lua_State *state) const {
  sol::table mobile = sol::state_view(state).create_named_table("Mobile");

  mobile.set_function(
      "touch",
      [&host](const std::string &nodeId, sol::this_state lua) -> int {
        lua_State *localState = lua;
        const auto center = host.mobileNodeCenterViewport(nodeId);
        if (!center) {
          return luaL_error(localState, "Mobile.touch: unknown HUD node '%s'.",
                            nodeId.c_str());
        }
        host.mobileEnqueueTap(*center);
        return lua_yield(localState, 0);
      });

  mobile.set_function(
      "tap",
      [&host](float x, float y, sol::this_state lua) -> int {
        lua_State *localState = lua;
        host.mobileEnqueueTap(host.mobileCanvasToViewport(Vec2{x, y}));
        return lua_yield(localState, 0);
      });

  mobile.set_function(
      "swipe",
      [&host](const std::string &fromId, const std::string &toId,
              sol::optional<double> duration, sol::this_state lua) -> int {
        lua_State *localState = lua;
        const auto from = host.mobileNodeCenterViewport(fromId);
        const auto to = host.mobileNodeCenterViewport(toId);
        if (!from || !to) {
          return luaL_error(localState, "Mobile.swipe: unknown HUD node '%s'.",
                            from ? toId.c_str() : fromId.c_str());
        }
        host.mobileEnqueueSwipe(*from, *to, duration.value_or(0.4));
        return lua_yield(localState, 0);
      });

  mobile.set_function(
      "swipe_xy",
      [&host](float fromX, float fromY, float toX, float toY,
              sol::optional<double> duration, sol::this_state lua) -> int {
        lua_State *localState = lua;
        host.mobileEnqueueSwipe(
            host.mobileCanvasToViewport(Vec2{fromX, fromY}),
            host.mobileCanvasToViewport(Vec2{toX, toY}),
            duration.value_or(0.4));
        return lua_yield(localState, 0);
      });

  mobile.set_function(
      "wait",
      [&host](double seconds, sol::this_state lua) -> int {
        lua_State *localState = lua;
        host.mobileWaitFor(seconds);
        return lua_yield(localState, 0);
      });

  mobile.set_function(
      "expect_scene",
      [&host](const std::string &sceneId, sol::optional<double> timeout,
              sol::this_state lua) -> int {
        lua_State *localState = lua;
        host.mobileExpectSceneStart(sceneId, timeout.value_or(10.0));
        return lua_yield(localState, 0);
      });

  mobile.set_function(
      "node_center",
      [&host, state](const std::string &nodeId) -> sol::object {
        const auto center = host.mobileNodeCenterCanvas(nodeId);
        if (!center)
          return sol::make_object(state, sol::nil);
        sol::state_view view(state);
        return sol::make_object(
            view, sol::as_table(std::vector<float>{center->x, center->y}));
      });

  mobile.set_function(
      "expect",
      [](bool condition, const std::string &message, sol::this_state lua)
          -> int {
        lua_State *localState = lua;
        if (!condition)
          return luaL_error(localState, "%s", message.c_str());
        return 0;
      });
}

} // namespace demi::runtime
