#include "demi/runtime/scripting/LuaE2ETestRunner.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <iostream>
#include <memory>

namespace {

using namespace demi::runtime;

LuaE2ETestRunner &runnerFromLua(lua_State *state) {
  return *static_cast<LuaE2ETestRunner *>(
      lua_touserdata(state, lua_upvalueindex(1)));
}

bool runTests() {
  std::unique_ptr<lua_State, decltype(&lua_close)> vm(luaL_newstate(),
                                                      lua_close);
  if (!vm) {
    return false;
  }
  lua_State *state = vm.get();
  luaL_openlibs(state);
  LuaE2ETestRunner runner;
  std::string scene = "first";
  int quits = 0;
  runner.initialize(
      state,
      {
          .nodeCenterCanvas = [](const std::string &id) -> std::optional<Vec2> {
            if (id == "button") {
              return Vec2{10.0F, 20.0F};
            }
            return std::nullopt;
          },
          .canvasToViewport =
              [](Vec2 position) {
                return Vec2{position.x * 2.0F, position.y * 2.0F};
              },
          .activeScene = [&scene] { return scene; },
          .requestQuit = [&quits] { ++quits; },
      });
  auto bind = [&](const char *name, lua_CFunction function) {
    lua_pushlightuserdata(state, &runner);
    lua_pushcclosure(state, function, 1);
    lua_setglobal(state, name);
  };
  bind("wait", [](lua_State *local) -> int {
    runnerFromLua(local).waitFor(luaL_checknumber(local, 1));
    return lua_yield(local, 0);
  });
  bind("tap", [](lua_State *local) -> int {
    runnerFromLua(local).enqueueTap({10.0F, 20.0F});
    return lua_yield(local, 0);
  });
  bind("expect_scene", [](lua_State *local) -> int {
    runnerFromLua(local).expectScene(luaL_checkstring(local, 1), 0.25);
    return lua_yield(local, 0);
  });
  if (luaL_dostring(state, R"lua(
    threads = setmetatable({}, {__mode = "v"})
    package.preload.sequence = function()
      return {tests = {{name = "sequence", func = function()
        threads[1] = coroutine.running()
        wait(0.5)
        tap()
        expect_scene("next")
      end}}}
    end
    package.preload.failures = function()
      return {tests = {
        {name = "timeout", func = function() expect_scene("missing") end},
        {name = "unexpected yield", func = function() coroutine.yield() end},
        {name = "lua error", func = function() error("failure probe") end},
        {name = "continues", func = function() end}
      }}
    end
    package.preload.empty = function() return {tests = {}} end
    package.preload.invalid = function() return {} end
  )lua") != LUA_OK) {
    return false;
  }
  const auto center = runner.nodeCenterViewport("button");
  if (!center || center->x != 20.0F || center->y != 40.0F ||
      runner.nodeCenterCanvas("missing")) {
    std::cerr << "Runner HUD callback mapping failed.\n";
    return false;
  }

  InputState input;
  runner.start("sequence");
  runner.update(0.0);
  runner.update(0.25);
  runner.drainSyntheticTouches(input);
  if (!input.touches.empty()) {
    std::cerr << "Seconds wait resumed early.\n";
    return false;
  }
  runner.update(0.25);
  runner.update(10.0); // Elapsed time cannot complete a queued gesture.
  runner.drainSyntheticTouches(input);
  if (input.touches.size() != 1 ||
      input.touches.front().phase != TouchPhase::Began) {
    return false;
  }
  const auto finger = input.touches.front().id;
  runner.update(0.0);
  runner.drainSyntheticTouches(input);
  if (input.touches.size() != 1 || input.touches.front().id != finger ||
      input.touches.front().phase != TouchPhase::Ended) {
    return false;
  }
  runner.update(0.0);
  scene = "next";
  runner.update(0.0);
  runner.update(0.0);
  runner.update(1.0);
  if (runner.active() || runner.passed() != 1 || runner.failed() != 0 ||
      quits != 1 || lua_gettop(state) != 0) {
    std::cerr << "Runner sequence or exactly-once completion failed.\n";
    return false;
  }
  if (luaL_dostring(state,
                    "collectgarbage('collect'); assert(threads[1] == nil)") !=
      LUA_OK) {
    std::cerr << "Completed coroutine remained rooted.\n";
    return false;
  }

  runner.start("failures");
  for (int frame = 0; frame < 20 && runner.active(); ++frame) {
    runner.update(0.125);
  }
  if (runner.active() || runner.passed() != 1 || runner.failed() != 3 ||
      quits != 2) {
    std::cerr << "Timeout/error continuation or restart counters failed.\n";
    return false;
  }
  runner.start("invalid");
  if (runner.active() || runner.failed() != 1 || quits != 3 ||
      lua_gettop(state) != 0) {
    std::cerr << "Invalid module did not complete with a failed summary.\n";
    return false;
  }
  runner.start("missing_module");
  if (runner.active() || runner.failed() != 1 || quits != 4 ||
      lua_gettop(state) != 0) {
    return false;
  }
  runner.start("empty");
  runner.update(0.0);
  if (runner.active() || runner.passed() != 0 || runner.failed() != 0 ||
      quits != 5) {
    return false;
  }

  input.touches.clear();
  runner.enqueueSwipe({0.0F, 0.0F}, {8.0F, 4.0F}, 0.05);
  runner.drainSyntheticTouches(input);
  if (input.touches.front().phase != TouchPhase::Began) {
    return false;
  }
  runner.drainSyntheticTouches(input);
  if (input.touches.front().phase != TouchPhase::Moved ||
      input.touches.front().position.x != 4.0F) {
    return false;
  }
  runner.drainSyntheticTouches(input);
  if (input.touches.front().phase != TouchPhase::Ended ||
      input.touches.front().position.x != 8.0F) {
    return false;
  }

  runner.start("sequence");
  runner.update(0.0);
  runner.update(0.5); // Suspend with an outstanding coroutine and touch queue.
  runner.shutdown();
  input.touches.clear();
  runner.drainSyntheticTouches(input);
  runner.update(1.0);
  if (runner.active() || !input.touches.empty() || quits != 5 ||
      luaL_dostring(state,
                    "collectgarbage('collect'); assert(threads[1] == nil)") !=
          LUA_OK) {
    std::cerr
        << "Shutdown retained coroutine references, callbacks, or touches.\n";
    return false;
  }
  vm.reset(); // Runner destruction is safe after explicit shutdown and VM
              // close.
  return true;
}

} // namespace

int main() {
  if (!runTests()) {
    std::cerr << "Lua E2E runner tests failed.\n";
    return 1;
  }
  return 0;
}
