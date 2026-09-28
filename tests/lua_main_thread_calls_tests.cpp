#include "demi/runtime/scripting/LuaMainThreadCalls.h"
#include "demi/runtime/scripting/LuaSharedMap.h"
#include "demi/runtime/scripting/LuaTaskFunction.h"
#include "demi/runtime/scripting/LuaWorkerContext.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace demi::runtime;
using namespace std::chrono_literals;

struct VM {
  lua_State *state = luaL_newstate();
  VM() {
    assert(state);
    luaL_openlibs(state);
    installLuaSharedBindings(state);
  }
  ~VM() { lua_close(state); }
};

void run(lua_State *state, const char *source) {
  if (luaL_dostring(state, source) != LUA_OK) {
    std::cerr << lua_tostring(state, -1) << '\n';
    std::abort();
  }
}

template <class Action> void finish(std::future<void> &worker, Action action) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (worker.wait_for(1ms) != std::future_status::ready) {
    assert(std::chrono::steady_clock::now() < deadline);
    action();
  }
  worker.get();
}

void captureAndEnvironment() {
  VM source, destination;
  run(source.state, R"lua(
    local n = 7
    local map = require('demi.shared').map({value=4})
    return function(input)
      input.changed = true
      return n + map:get('value') + offset, nil, input, 'a\0b'
    end, {value=3}
  )lua");
  const auto function = captureLuaTaskFunction(source.state, 1, 2, 1);
  assert(lua_gettop(source.state) == 2);
  run(destination.state, "offset=2; return {offset=5}");
  assert(invokeLuaTaskFunction(destination.state, function, 1) == 4);
  assert(lua_tointeger(destination.state, 2) == 16);
  assert(lua_isnil(destination.state, 3));
  std::size_t length = 0;
  const char *bytes = lua_tolstring(destination.state, 5, &length);
  assert(length == 3 && bytes[1] == '\0');
  lua_settop(destination.state, 0);
  assert(invokeLuaTaskFunction(destination.state, function) == 4);
  assert(lua_tointeger(destination.state, 1) == 13);
  lua_getfield(source.state, 2, "changed");
  assert(lua_isnil(source.state, -1));

  for (const char *script :
       {"local self={}; return function() return self end",
        "local f=function() end; return function() return f() end",
        "local _ENV={}; return function() return missing end",
        "return print"}) {
    lua_settop(source.state, 0);
    run(source.state, script);
    bool rejected = false;
    try {
      (void)captureLuaTaskFunction(source.state, 1, 2, 0);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected && lua_gettop(source.state) == 1);
  }
  lua_settop(source.state, 0);
  run(source.state, "return function() error('callback failure') end");
  const auto failing = captureLuaTaskFunction(source.state, 1, 2, 0);
  const int top = lua_gettop(destination.state);
  bool rejected = false;
  try {
    invokeLuaTaskFunction(destination.state, failing);
  } catch (const std::runtime_error &error) {
    rejected =
        std::string(error.what()).find("callback failure") != std::string::npos;
  }
  assert(rejected && lua_gettop(destination.state) == top);
}

void handoff() {
  VM main;
  LuaMainThreadCalls calls;
  calls.attach(main.state);
  run(main.state, R"lua(
    count=0
    package.preload['demi.test_service']=function()
      return {apply=function(value) count=count+1; return value+10 end}
    end
  )lua");
  assert(calls.call(main.state) == 2);
  assert(lua_isnil(main.state, -2));
  lua_settop(main.state, 0);
  auto worker = std::async(std::launch::async, [&] {
    VM vm;
    setLuaWorkerContext(vm.state, {});
    run(vm.state, R"lua(
      return function(input)
        local service=require('demi.test_service')
        input.self=input
        return service.apply(input.value), nil, input
      end, {value=5}
    )lua");
    assert(calls.call(vm.state) == 3);
    assert(lua_tointeger(vm.state, -3) == 15 && lua_isnil(vm.state, -2));
    lua_getfield(vm.state, -1, "self");
    assert(lua_rawequal(vm.state, -1, -2));
    lua_settop(vm.state, 0);
    run(vm.state, "return function() error('handoff failure') end");
    assert(calls.call(vm.state) == 2);
    assert(lua_isnil(vm.state, -2));
    assert(std::string(lua_tostring(vm.state, -1)).find("handoff failure") !=
           std::string::npos);
    lua_settop(vm.state, 0);
    run(vm.state, "return function() error('', 0) end");
    assert(calls.call(vm.state) == 2 && lua_isnil(vm.state, -2));
    assert(std::string(lua_tostring(vm.state, -1)).empty());
    lua_settop(vm.state, 0);
    run(vm.state, "local self={}; return function() return self end");
    assert(calls.call(vm.state) == 2 && lua_isnil(vm.state, -2));
    assert(std::string(lua_tostring(vm.state, -1)).find("self") !=
           std::string::npos);
    lua_settop(vm.state, 0);
    run(vm.state, "return function() return function() end end");
    assert(calls.call(vm.state) == 2 && lua_isnil(vm.state, -2));
    lua_settop(vm.state, 0);
    run(vm.state, "return function() end");
    assert(calls.call(vm.state) == 0);
  });
  finish(worker, [&] {
    calls.dispatch();
    assert(lua_gettop(main.state) == 0);
  });
  run(main.state, "assert(count==1)");
  calls.shutdown();
}

void cancellationAndReuse() {
  VM main;
  LuaMainThreadCalls calls;
  calls.attach(main.state);
  run(main.state, "count=0");
  std::stop_source stop;
  auto worker = std::async(std::launch::async, [&] {
    VM vm;
    setLuaWorkerContext(vm.state, stop.get_token());
    run(vm.state, "return function() count=count+1 end");
    assert(calls.call(vm.state) == 2);
    assert(lua_isnil(vm.state, -2));
    assert(std::string(lua_tostring(vm.state, -1)) == "cancelled");
  });
  stop.request_stop();
  finish(worker, [] {});
  calls.dispatch();
  run(main.state, "assert(count==0)");

  auto queued = std::async(std::launch::async, [&] {
    VM vm;
    setLuaWorkerContext(vm.state, {});
    run(vm.state, "return function() count=count+1 end");
    assert(calls.call(vm.state) == 2);
    assert(std::string(lua_tostring(vm.state, -1)) == "cancelled");
  });
  finish(queued, [&] { calls.cancelPending(); });
  calls.dispatch();
  run(main.state, "assert(count==0)");

  auto next = std::async(std::launch::async, [&] {
    VM vm;
    setLuaWorkerContext(vm.state, {});
    run(vm.state, "return function() count=count+1; return count end");
    assert(calls.call(vm.state) == 1 && lua_tointeger(vm.state, -1) == 1);
  });
  finish(next, [&] { calls.dispatch(); });
  calls.shutdown();
  VM vm;
  setLuaWorkerContext(vm.state, {});
  run(vm.state, "return function() end");
  assert(calls.call(vm.state) == 2);
  assert(std::string(lua_tostring(vm.state, -1)) == "Task.main is shut down");
}

int stopInsideCallback(lua_State *state) {
  auto *stop = static_cast<std::stop_source *>(
      lua_touserdata(state, lua_upvalueindex(1)));
  auto *calls = static_cast<LuaMainThreadCalls *>(
      lua_touserdata(state, lua_upvalueindex(2)));
  stop->request_stop();
  calls->cancelPending();
  return 0;
}

void executingCancellationKeepsWrites() {
  VM main;
  LuaMainThreadCalls calls;
  calls.attach(main.state);
  std::stop_source stop;
  lua_pushlightuserdata(main.state, &stop);
  lua_pushlightuserdata(main.state, &calls);
  lua_pushcclosure(main.state, stopInsideCallback, 2);
  lua_setglobal(main.state, "stop_now");
  auto worker = std::async(std::launch::async, [&] {
    VM vm;
    setLuaWorkerContext(vm.state, stop.get_token());
    run(vm.state,
        "return function() before=true; stop_now(); after=true; return 42 end");
    assert(calls.call(vm.state) == 2);
    assert(std::string(lua_tostring(vm.state, -1)) == "cancelled");
  });
  finish(worker, [&] { calls.dispatch(); });
  run(main.state, "assert(before and after)");
  calls.shutdown();
}
} // namespace

int main() {
  captureAndEnvironment();
  handoff();
  cancellationAndReuse();
  executingCancellationKeepsWrites();
  std::cout << "Lua main-thread calls tests passed\n";
}
