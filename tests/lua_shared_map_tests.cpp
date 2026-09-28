#include "demi/runtime/scripting/LuaSharedMap.h"
#include "demi/runtime/scripting/lua_compat/lua.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace demi::runtime;
namespace {
void run(lua_State *L, const char *script) {
  if (luaL_dostring(L, script) != LUA_OK) {
    std::cerr << lua_tostring(L, -1) << '\n';
    std::abort();
  }
}
lua_State *vm(bool worker, const std::shared_ptr<LuaSharedMap> &map) {
  auto *L = luaL_newstate();
  assert(L);
  luaL_openlibs(L);
  installLuaSharedBindings(L, worker);
  pushLuaSharedMap(L, map);
  assert(luaSharedMap(L, -1) == map);
  lua_setglobal(L, "map");
  return L;
}
} // namespace

int main() {
  auto map = std::make_shared<LuaSharedMap>();
  auto *main = vm(false, map);
  run(main, R"lua(
    local Shared = require('demi.shared')
    local initial = {binary='a\0b', number=3.25, integer=42, flag=false}
    local m = assert(Shared.map(initial))
    initial.integer=99
    assert(m:get('integer')==42 and m:get('binary')=='a\0b')
    assert(m:get('flag')==false and m:get('number')==3.25)
    for _,kind in ipairs({'array','object','null'}) do
      local marked=setmetatable({}, {__demi_json_kind=kind, __metatable=false})
      assert(m:set(kind,marked))
      assert(getmetatable(m:get(kind))==false)
    end
    local cycle={}; cycle.self=cycle
    assert(m:set('cycle',cycle))
    local copy=m:get('cycle'); assert(copy.self==copy)
    copy.changed=true; assert(m:get('cycle').changed==nil)
    assert(m:set('nil',nil)); assert(m:get('nil')==nil)
    assert(not Shared.map({[1]='bad'}))
    assert(not Shared.map('bad'))
    for _,v in ipairs({function() end, coroutine.create(function() end), io.stdout, m, {nested=m}}) do
      local ok,err=m:set('bad',v); assert(ok==nil and type(err)=='string')
    end
    assert(not m:set(1,2)); assert(not m:add('flag',1))
    assert(not m:add('integer','1')); assert(not m:add('integer',math.huge))
    assert(m:get('integer')==42)
    assert(m:set('max',math.maxinteger)); assert(not m:add('max',1))
    assert(m:get('max')==math.maxinteger)
    assert(m:set('min',math.mininteger)); assert(not m:add('min',-1))
    assert(m:get('min')==math.mininteger)
    assert(m:add('new',0.5)==0.5)
    assert(not m:set('unknown',setmetatable({}, {__index={}})))
    local ok,a,b,c=m:with_lock(function(self)
      assert(self==m)
      assert(self:with_lock(function() assert(self:add('integer',1)==43) end))
      return 1,nil,3
    end)
    assert(ok and a==1 and b==nil and c==3)
    local snapshot=m:snapshot(); snapshot.integer=999
    assert(m:get('integer')==43)
    assert(map:set('compound',0)); assert(map:set('atomic',0))
  )lua");

  // Retained immutable values survive replacement without holding the lock.
  std::shared_ptr<const LuaTransferGraph> previous;
  {
    std::lock_guard lock(map->mutex);
    previous = map->values.at("atomic");
  }
  run(main, "assert(map:set('atomic',7))");
  assert(previous->roots.at(0).integer == 0);
  {
    std::lock_guard lock(map->mutex);
    assert(map->values.at("atomic") != previous);
    assert(map->values.at("atomic")->roots.at(0).integer == 7);
  }
  run(main, "assert(map:set('atomic',0))");

  std::atomic<bool> start = false;
  auto increment = [&] {
    auto *L = vm(true, map);
    while (!start.load())
      std::this_thread::yield();
    run(L, R"lua(
      for i=1,2000 do
        assert(map:with_lock(function(m)
          local old=assert(m:get('compound'))
          assert(m:set('compound',old+1))
        end))
        assert(map:add('atomic',1))
      end
    )lua");
    lua_close(L);
  };
  std::thread first(increment), second(increment);
  start = true;
  first.join();
  second.join();
  run(main, "assert(map:get('compound')==4000 and map:get('atomic')==4000)");

  // A different OS thread must hold the recursive lock to test contention.
  std::atomic<bool> held = false, release = false;
  std::thread holder([&] {
    std::lock_guard lock(map->mutex);
    held = true;
    while (!release.load())
      std::this_thread::yield();
  });
  while (!held.load())
    std::this_thread::yield();
  const auto begin = std::chrono::steady_clock::now();
  run(main, R"lua(
    local function busy(a,b) assert(not a and b=='busy') end
    busy(map:get('compound')); busy(map:set('compound',9))
    busy(map:add('atomic',1)); busy(map:snapshot())
    busy(map:with_lock(function() error('must not run') end))
    busy(map:try_lock(function() error('must not run') end))
  )lua");
  assert(std::chrono::steady_clock::now() - begin < std::chrono::seconds(1));
  std::atomic<bool> cancel = false, waiting = false;
  std::thread waiter([&] {
    auto *L = vm(true, map);
    setLuaSharedCancellation(L, &cancel);
    run(L, "local ok,err=map:try_lock(function() end); assert(ok==false and "
           "err=='busy')");
    waiting = true;
    run(L, "local ok,err=map:with_lock(function() error('must not run') end); "
           "assert(ok==false and err=='cancelled')");
    run(L, "local value,err=map:get('atomic'); assert(value==nil and "
           "err=='cancelled')");
    setLuaSharedCancellation(L, nullptr);
    lua_close(L);
  });
  while (!waiting.load())
    std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  cancel = true;
  waiter.join(); // must finish while the holder still owns the mutex
  release = true;
  holder.join();

  run(main, R"lua(
    local ok,err=pcall(function() map:with_lock(function() error('callback failed') end) end)
    assert(not ok and err:find('callback failed',1,true))
    local co=coroutine.create(function()
      map:with_lock(function() coroutine.yield('illegal') end)
    end)
    local resumed,message=coroutine.resume(co)
    assert(not resumed and message:find('yield',1,true))
  )lua");
  // Reentry on the same thread cannot prove unlock, so probe from another VM.
  std::thread probe([&] {
    auto *L = vm(true, map);
    run(L, "assert(map:try_lock(function(m) assert(m:add('atomic',1)==4001) "
           "end))");
    lua_close(L);
  });
  probe.join();

  lua_getglobal(main, "map");
  const auto graph = captureLuaValues(main, -1, 1);
  lua_pop(main, 1);
  auto *other = vm(false, map);
  pushLuaValues(other, graph);
  assert(luaSharedMap(other, -1) == map);
  lua_pop(other, 1);
  lua_close(other);
  lua_close(main);
  std::cout << "Lua shared map tests passed\n";
}
