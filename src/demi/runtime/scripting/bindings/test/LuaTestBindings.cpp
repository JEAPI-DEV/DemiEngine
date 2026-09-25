#include "demi/runtime/scripting/bindings/test/LuaTestBindings.h"

#include "demi/runtime/scripting/LuaScriptHost.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <sol/sol.hpp>

#include <string>

namespace demi::runtime {

void LuaTestBindingModule::install(LuaScriptHost &host,
                                   lua_State *state) const {
  auto &runner = host.e2eTestRunner();
  sol::table test = sol::state_view(state).create_named_table("Test");

  test.set_function(
      "touch",
      [&runner](const std::string &nodeId, sol::this_state lua) -> int {
        lua_State *localState = lua;
        const auto center = runner.nodeCenterViewport(nodeId);
        if (!center) {
          return luaL_error(localState, "Test.touch: unknown HUD node '%s'.",
                            nodeId.c_str());
        }
        runner.enqueueTap(*center);
        return lua_yield(localState, 0);
      });

  test.set_function("tap",
                    [&runner](float x, float y, sol::this_state lua) -> int {
                      lua_State *localState = lua;
                      runner.enqueueTap(runner.canvasToViewport(Vec2{x, y}));
                      return lua_yield(localState, 0);
                    });

  test.set_function(
      "swipe",
      [&runner](const std::string &fromId, const std::string &toId,
                sol::optional<double> duration, sol::this_state lua) -> int {
        lua_State *localState = lua;
        const auto from = runner.nodeCenterViewport(fromId);
        const auto to = runner.nodeCenterViewport(toId);
        if (!from || !to) {
          return luaL_error(localState, "Test.swipe: unknown HUD node '%s'.",
                            from ? toId.c_str() : fromId.c_str());
        }
        runner.enqueueSwipe(*from, *to, duration.value_or(0.4));
        return lua_yield(localState, 0);
      });

  test.set_function(
      "swipe_xy",
      [&runner](float fromX, float fromY, float toX, float toY,
                sol::optional<double> duration, sol::this_state lua) -> int {
        lua_State *localState = lua;
        runner.enqueueSwipe(runner.canvasToViewport(Vec2{fromX, fromY}),
                            runner.canvasToViewport(Vec2{toX, toY}),
                            duration.value_or(0.4));
        return lua_yield(localState, 0);
      });

  test.set_function("wait",
                    [&runner](double seconds, sol::this_state lua) -> int {
                      lua_State *localState = lua;
                      runner.waitFor(seconds);
                      return lua_yield(localState, 0);
                    });

  test.set_function("expect_scene",
                    [&runner](const std::string &sceneId,
                              sol::optional<double> timeout,
                              sol::this_state lua) -> int {
                      lua_State *localState = lua;
                      runner.expectScene(sceneId, timeout.value_or(10.0));
                      return lua_yield(localState, 0);
                    });

  test.set_function(
      "node_center",
      [&runner, state](const std::string &nodeId) -> sol::object {
        const auto center = runner.nodeCenterCanvas(nodeId);
        if (!center)
          return sol::make_object(state, sol::nil);
        sol::state_view view(state);
        return sol::make_object(
            view, sol::as_table(std::vector<float>{center->x, center->y}));
      });

  test.set_function("expect",
                    [](bool condition, const std::string &message,
                       sol::this_state lua) -> int {
                      lua_State *localState = lua;
                      if (!condition)
                        return luaL_error(localState, "%s", message.c_str());
                      return 0;
                    });
}

} // namespace demi::runtime
