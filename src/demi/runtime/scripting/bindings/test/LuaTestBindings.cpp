#include "demi/runtime/scripting/bindings/test/LuaTestBindings.h"

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

void LuaTestBindingModule::install(LuaScriptHost &host,
                                         lua_State *state) const {
  sol::table test = sol::state_view(state).create_named_table("Test");

  test.set_function(
      "touch",
      [&host](const std::string &nodeId, sol::this_state lua) -> int {
        lua_State *localState = lua;
        const auto center = host.e2eNodeCenterViewport(nodeId);
        if (!center) {
          return luaL_error(localState, "Test.touch: unknown HUD node '%s'.",
                            nodeId.c_str());
        }
        host.e2eEnqueueTap(*center);
        return lua_yield(localState, 0);
      });

  test.set_function(
      "tap",
      [&host](float x, float y, sol::this_state lua) -> int {
        lua_State *localState = lua;
        host.e2eEnqueueTap(host.e2eCanvasToViewport(Vec2{x, y}));
        return lua_yield(localState, 0);
      });

  test.set_function(
      "swipe",
      [&host](const std::string &fromId, const std::string &toId,
              sol::optional<double> duration, sol::this_state lua) -> int {
        lua_State *localState = lua;
        const auto from = host.e2eNodeCenterViewport(fromId);
        const auto to = host.e2eNodeCenterViewport(toId);
        if (!from || !to) {
          return luaL_error(localState, "Test.swipe: unknown HUD node '%s'.",
                            from ? toId.c_str() : fromId.c_str());
        }
        host.e2eEnqueueSwipe(*from, *to, duration.value_or(0.4));
        return lua_yield(localState, 0);
      });

  test.set_function(
      "swipe_xy",
      [&host](float fromX, float fromY, float toX, float toY,
              sol::optional<double> duration, sol::this_state lua) -> int {
        lua_State *localState = lua;
        host.e2eEnqueueSwipe(
            host.e2eCanvasToViewport(Vec2{fromX, fromY}),
            host.e2eCanvasToViewport(Vec2{toX, toY}),
            duration.value_or(0.4));
        return lua_yield(localState, 0);
      });

  test.set_function(
      "wait",
      [&host](double seconds, sol::this_state lua) -> int {
        lua_State *localState = lua;
        host.e2eWaitFor(seconds);
        return lua_yield(localState, 0);
      });

  test.set_function(
      "expect_scene",
      [&host](const std::string &sceneId, sol::optional<double> timeout,
              sol::this_state lua) -> int {
        lua_State *localState = lua;
        host.e2eExpectSceneStart(sceneId, timeout.value_or(10.0));
        return lua_yield(localState, 0);
      });

  test.set_function(
      "node_center",
      [&host, state](const std::string &nodeId) -> sol::object {
        const auto center = host.e2eNodeCenterCanvas(nodeId);
        if (!center)
          return sol::make_object(state, sol::nil);
        sol::state_view view(state);
        return sol::make_object(
            view, sol::as_table(std::vector<float>{center->x, center->y}));
      });

  test.set_function(
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
