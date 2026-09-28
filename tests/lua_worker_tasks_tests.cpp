#include "demi/runtime/scripting/LuaSharedMap.h"
#include "demi/runtime/scripting/LuaWorkerTasks.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {
using namespace demi::runtime;
using namespace std::chrono_literals;

void run(lua_State *state, const char *script) {
  if (luaL_dostring(state, script) != LUA_OK) {
    std::cerr << lua_tostring(state, -1) << "\nScript:\n" << script << '\n';
    std::abort();
  }
}

bool evaluate(lua_State *state, const char *expression) {
  const std::string script = std::string("return ") + expression;
  if (luaL_dostring(state, script.c_str()) != LUA_OK) {
    std::cerr << lua_tostring(state, -1) << '\n';
    std::abort();
  }
  const bool value = lua_toboolean(state, -1);
  lua_pop(state, 1);
  return value;
}

template <class Predicate> void waitFor(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      std::cerr << "Lua worker test timed out\n";
      std::abort();
    }
    std::this_thread::sleep_for(1ms);
  }
}

void waitFor(lua_State *state, const char *expression) {
  waitFor([&] { return evaluate(state, expression); });
}

struct Fixture {
  lua_State *state = luaL_newstate();
  LuaWorkerTasks tasks;

  Fixture() {
    assert(state);
    luaL_openlibs(state);
    run(state,
        "original_resume=coroutine.resume; original_sethook=debug.sethook");
    tasks.attach(state);
    installLuaTaskBindings(state, tasks);
    installLuaSharedBindings(state, false);
    run(state, R"lua(
      Shared = require('demi.shared')
      assert(coroutine.resume == original_resume and debug.sethook == original_sethook)
      assert(Task.start == nil and Task.async == nil and Task.await == nil)
      assert(Task.yield == nil and Task.sleep == nil)
      assert(not Task.cancelled() and Task.check_cancelled()==nil)
      Input = {poll = function() main_updates = main_updates + 1 end}
      main_updates = 0
    )lua");
  }

  ~Fixture() {
    tasks.shutdown();
    lua_close(state);
  }
};

std::shared_ptr<LuaSharedMap> shared(lua_State *state, const char *name) {
  lua_getglobal(state, name);
  auto map = luaSharedMap(state, -1);
  lua_pop(state, 1);
  assert(map);
  return map;
}

void checkParallelWorkersAndMainInput() {
  Fixture fixture;
  auto *state = fixture.state;
  run(state, R"lua(
    barrier = Shared.map({arrived=0, release=false})
    lock_one = Shared.map()
    lock_two = Shared.map()
    local function worker(lock, gate)
      local Task = require('demi.task')
      local entered, value = lock:with_lock(function()
        assert(gate:add('arrived', 1))
        for i=1,100000000 do
          Task.check_cancelled()
          if gate:get('release') then return 73 end
        end
        error('barrier timed out')
      end)
      assert(entered, value)
      return value
    end
    first = Task.fork(worker, lock_one, barrier)
    second = Task.fork(worker, lock_two, barrier)
  )lua");
  // Neither worker can finish until both are running and the main thread
  // releases the barrier. A main-thread coroutine scheduler cannot reach it.
  waitFor(state, "barrier:get('arrived') == 2");
  const auto firstLock = shared(state, "lock_one");
  const auto secondLock = shared(state, "lock_two");
  const bool mainOwnsFirst = firstLock->mutex.try_lock();
  if (mainOwnsFirst)
    firstLock->mutex.unlock();
  const bool mainOwnsSecond = secondLock->mutex.try_lock();
  if (mainOwnsSecond)
    secondLock->mutex.unlock();
  assert(!mainOwnsFirst && !mainOwnsSecond);
  run(state, R"lua(
    for i=1,1000 do Input.poll() end
    assert(main_updates==1000 and not first:done() and not second:done())
    local value, err = first:result()
    assert(value==nil and err=='pending' and first:error()==nil)
  )lua");
  waitFor(state, "barrier:set('release', true)");
  waitFor(state, "first:done() and second:done()");
  run(state, "assert(first:result()==73 and second:result()==73)");
}

void checkSnapshotsAndCaptureValidation() {
  Fixture fixture;
  auto *state = fixture.state;
  run(state, R"lua(
    assert(not pcall(Task.configure, {workers=0}))
    assert(not pcall(Task.configure, {workers=1.5}))
    assert(not pcall(Task.configure, {workers='2'}))
    assert(not pcall(Task.configure, {workers=2, slice_ms=1}))
    assert(not pcall(Task.fork, print))
    local values={secret=2}
    local ok,err=pcall(Task.fork, function() return values.secret end)
    assert(not ok and err:find('values',1,true) and err:find('table',1,true), err)
    local callback=function() return 1 end
    ok,err=pcall(Task.fork, function() return callback() end)
    assert(not ok and err:find('function',1,true), err)
    local file=assert(io.tmpfile())
    ok,err=pcall(Task.fork, function() return file end)
    file:close()
    assert(not ok and err:find('userdata',1,true), err)
    local custom=assert(load('return function() return math.pi end', 'custom', 't',
                             setmetatable({}, {__index=_G})))()
    ok,err=pcall(Task.fork, custom)
    assert(not ok and err:find('custom _ENV',1,true), err)
    assert(not pcall(Task.fork, function(x) return x end, function() end))
    assert(not pcall(Task.fork, function(x) return x end, {fn=callback}))
    Task.configure({workers=1}) -- Rejected forks must not start the pool.

    gate=Shared.map({release=false})
    blocker=Task.fork(function(gate)
      local Task=require('demi.task')
      for i=1,100000000 do
        Task.check_cancelled()
        if gate:get('release') then return true end
      end
      error('snapshot gate timed out')
    end, gate)
    source={value=7, nested={name='original'}}
    source.self=source
    local offset,enabled,label,missing=5,true,'captured',nil
    local sharedCapture=gate
    local sameCapture=gate
    copied=Task.fork(function(a,b,n)
      assert(a==b and a.self==a and n==nil)
      assert(a.value==7 and a.nested.name=='original')
      assert(offset==5 and enabled and label=='captured' and missing==nil)
      assert(sharedCapture:get('release'))
      assert(sharedCapture==sameCapture)
      a.value=99
      return a, a, sharedCapture, nil, offset
    end, source, source, nil)
    offset,enabled,label=50,false,'changed'
    source.value=20
    source.nested.name='changed'
    assert(not pcall(Task.configure, {workers=2}))
  )lua");
  waitFor(state, "gate:set('release', true)");
  waitFor(state, "blocker:done() and copied:done()");
  run(state, R"lua(
    assert(copied:status()=='completed', copied:error())
    local a,b,map,missing,offset=copied:result()
    assert(a==b and a.self==a and a.value==99)
    assert(missing==nil and offset==5 and map:get('release'))
    assert(map==gate)
    assert(select('#',copied:result())==5)
    assert(source.value==20 and source.nested.name=='changed')
    a.value=1000
    local again=copied:result()
    assert(again.value==99 and again.self==again)
    assert(copied:error()==nil and not copied:cancel())
  )lua");
}

void checkErrorsAndResults() {
  Fixture fixture;
  auto *state = fixture.state;
  run(state, R"lua(
    failed=Task.fork(function() error('worker-error') end)
    invalid_result=Task.fork(function() return function() end end)
    nonstring=Task.fork(function() error({problem=true}) end)
    empty=Task.fork(function() end)
    nils=Task.fork(function() return nil,'answer',nil end)
    binary=Task.fork(function(value) return value end,'a\0b')
  )lua");
  waitFor(state, "failed:done() and invalid_result:done() and nonstring:done() "
                 "and empty:done() and nils:done() and binary:done()");
  run(state, R"lua(
    assert(failed:status()=='failed' and failed:error():find('worker-error',1,true))
    local value,err=failed:result()
    assert(value==nil and err==failed:error() and not failed:cancel())
    assert(invalid_result:status()=='failed' and invalid_result:error():find('functions',1,true))
    assert(nonstring:status()=='failed' and type(nonstring:error())=='string')
    assert(empty:status()=='completed' and select('#',empty:result())==0)
    assert(select('#',nils:result())==3)
    local a,b,c=nils:result(); assert(a==nil and b=='answer' and c==nil)
    assert(binary:result()=='a\0b')
  )lua");
}

void checkWorkerReuseAndIsolation() {
  Fixture fixture;
  auto *state = fixture.state;
  fixture.tasks.configure(1);
  for (int iteration = 0; iteration < 8; ++iteration) {
    run(state, R"lua(
      poisoned=Task.fork(function()
        assert(Input==nil and Entity==nil and Task==nil and Shared==nil)
        assert(io==nil and os==nil and package==nil and debug==nil and coroutine==nil)
        assert(load==nil and loadfile==nil and dofile==nil and collectgarbage==nil)
        assert(not pcall(require,'demi.input') and not pcall(require,'demi.network.http'))
        assert(not pcall(setmetatable, {}, {__gc=function() end}))
        local Task=require('demi.task')
        local Shared=require('demi.shared')
        assert(Task.fork==nil and Task.configure==nil and not Task.cancelled())
        assert(Task.check_cancelled()==nil)
        assert(leak==nil and _G.leak==nil and math.leak==nil)
        assert(string.upper('hello')=='HELLO' and ('hello'):upper()=='HELLO')
        assert(table.concat({'a','b'})=='ab' and utf8.len('hello')==5)
        assert(Task.leak==nil and Shared.leak==nil)
        leak='worker'
        _G.leak='worker'
        math.leak=true
        string.upper=nil
        getmetatable('').leak=true
        table.concat=nil
        utf8.len=nil
        Task.leak=true
        Shared.leak=true
        _G={replacement=true}
        return 17
      end)
    )lua");
    waitFor(state, "poisoned:done()");
    run(state, "assert(poisoned:status()=='completed',poisoned:error()); "
               "assert(poisoned:result()==17)");
  }
  run(state, R"lua(
    assert(leak==nil and math.leak==nil and string.upper('hello')=='HELLO')
    assert(getmetatable('').leak==nil)
    failure=Task.fork(function() math.leak=true; error('poison-error') end)
  )lua");
  waitFor(state, "failure:done()");
  run(state, R"lua(
    restored=Task.fork(function()
      assert(math.leak==nil and getmetatable('').leak==nil)
      assert(not require('demi.task').cancelled())
      return 29
    end)
  )lua");
  waitFor(state, "restored:done()");
  run(state, "assert(restored:result()==29, restored:error())");
}

void checkQueuedAndCooperativeCancellation() {
  Fixture fixture;
  auto *state = fixture.state;
  fixture.tasks.configure(1);
  run(state, R"lua(
    entered=Shared.map({started=false, writes=0})
    running=Task.fork(function(map)
      local Task=require('demi.task')
      assert(map:set('started',true))
      while not Task.cancelled() do end
      Task.check_cancelled()
      map:add('writes',1)
    end,entered)
  )lua");
  waitFor(state, "entered:get('started')");
  run(state, R"lua(
    queued=Task.fork(function(map) map:add('writes',1) end,entered)
    assert(queued:status()=='queued' and not queued:done())
    assert(queued:cancel() and queued:done() and queued:status()=='cancelled')
    assert(not queued:cancel() and queued:error()=='cancelled')
    local value,err=queued:result(); assert(value==nil and err=='cancelled')
    assert(running:cancel())
  )lua");
  waitFor(state, "running:done()");
  run(state,
      "assert(running:status()=='cancelled' and entered:get('writes')==0)");
  // The same worker must receive a cleared cancellation context next time.
  run(state, "next=Task.fork(function(map) assert(not "
             "require('demi.task').cancelled()); return map:add('writes',1) "
             "end,entered)");
  waitFor(state, "next:done()");
  run(state, "assert(next:result()==1 and entered:get('writes')==1)");
}

void checkRunningCancellationSettlesAfterExecution() {
  Fixture fixture;
  auto *state = fixture.state;
  fixture.tasks.configure(1);
  run(state, R"lua(
    execution_lock=Shared.map()
    started=Shared.map({entered=false})
    busy=Task.fork(function(lock,signal)
      local Task=require('demi.task')
      local ok,err=lock:with_lock(function()
        assert(signal:set('entered',true))
        while not Task.cancelled() do end
        -- Deliberately ignore cancellation for a bounded amount of work.
        local sum=0
        for i=1,100000000 do sum=sum+i end
        assert(sum>0)
      end)
      assert(ok,err)
    end,execution_lock,started)
  )lua");
  waitFor(state, "started:get('entered')");
  run(state, R"lua(
    assert(busy:cancel())
    assert(busy:status()=='cancelling' and not busy:done())
    local value,err=busy:result(); assert(value==nil and err=='pending')
    assert(not busy:cancel())
  )lua");
  const auto lock = shared(state, "execution_lock");
  const bool acquired = lock->mutex.try_lock();
  if (acquired)
    lock->mutex.unlock();
  assert(!acquired);
  // Scene cancellation must also return while the worker is still executing.
  fixture.tasks.cancelAll();
  run(state, "assert(not busy:done()); Input.poll(); assert(main_updates==1)");
  waitFor(state, "busy:done()");
  run(state,
      "assert(busy:status()=='cancelled' and busy:error()=='cancelled')");
  const bool settledLock = lock->mutex.try_lock();
  assert(settledLock);
  if (settledLock)
    lock->mutex.unlock();
}

void checkSceneCancellationAndShutdown() {
  Fixture fixture;
  auto *state = fixture.state;
  run(state, R"lua(
    started=Shared.map({count=0, writes=0})
    local function work(map)
      local Task=require('demi.task')
      assert(map:add('count',1))
      while true do Task.check_cancelled() end
    end
    a=Task.fork(work,started)
    b=Task.fork(work,started)
    c=Task.fork(function(map) map:add('writes',1) end,started)
  )lua");
  waitFor(state, "started:get('count')==2");
  fixture.tasks.cancelAll();
  waitFor(state, "a:done() and b:done() and c:done()");
  run(state, "assert(a:status()=='cancelled' and b:status()=='cancelled' and "
             "c:status()=='cancelled'); assert(started:get('writes')==0)");
  run(state, "new_scene=Task.fork(function() return 31 end)");
  waitFor(state, "new_scene:done()");
  run(state, "assert(new_scene:result()==31)");
  run(state, R"lua(
    active=Task.fork(function() local Task=require('demi.task'); while true do Task.check_cancelled() end end)
    queued=Task.fork(function() return 23 end)
  )lua");
  fixture.tasks.shutdown();
  fixture.tasks.shutdown();
  run(state, R"lua(
    assert(active:done() and queued:done() and active:status()=='cancelled')
    assert(not pcall(Task.fork,function() end))
    assert(not pcall(Task.configure,{workers=1}))
    assert(new_scene:result()==31)
  )lua");
}

void checkHandleLifetimeAndConcurrentReaders() {
  auto *state = luaL_newstate();
  assert(state);
  luaL_openlibs(state);
  std::shared_ptr<LuaWorkerTask> survivor;
  std::shared_ptr<LuaWorkerTask> cancelled;
  {
    LuaWorkerTasks tasks;
    tasks.attach(state);
    tasks.configure(1);
    installLuaTaskBindings(state, tasks);
    run(state, "lua_survivor=Task.fork(function() return 41 end)");
    if (luaL_loadstring(state, "local result={value=43}; result.self=result; "
                               "local Shared=require('demi.shared'); "
                               "local map=Shared.map({alive=1}); "
                               "return result,nil,result,map") != LUA_OK)
      std::abort();
    survivor = tasks.fork(state, -1, lua_gettop(state) + 1, 0);
    lua_pop(state, 1);
    std::atomic<bool> stop = false;
    std::thread reader([&] {
      while (!stop.load()) {
        const auto status = survivor->status();
        if (status == LuaWorkerTaskStatus::Completed)
          assert(survivor->done());
        static_cast<void>(survivor->error());
      }
    });
    waitFor([&] { return survivor->done(); });
    stop.store(true);
    reader.join();
    assert(survivor->status() == LuaWorkerTaskStatus::Completed);
    if (luaL_loadstring(state, "local Task=require('demi.task'); while true do "
                               "Task.check_cancelled() end") != LUA_OK)
      std::abort();
    cancelled = tasks.fork(state, -1, lua_gettop(state) + 1, 0);
    lua_pop(state, 1);
  } // Destructor joins running work while native and Lua handles stay alive.
  run(state, R"lua(
    assert(lua_survivor:done() and lua_survivor:result()==41)
    assert(not lua_survivor:cancel())
    assert(not pcall(Task.fork,function() end))
  )lua");
  lua_close(state);
  assert(cancelled->done() &&
         cancelled->status() == LuaWorkerTaskStatus::Cancelled);
  assert(!cancelled->cancel() && !survivor->cancel());

  // Transfer results to a new main VM after the original host and VM are gone.
  state = luaL_newstate();
  assert(state);
  luaL_openlibs(state);
  const int resultCount = survivor->pushResult(state);
  assert(resultCount == 4);
  assert(lua_isnil(state, 2) && lua_rawequal(state, 1, 3));
  const auto survivingMap = luaSharedMap(state, 4);
  assert(survivingMap);
  {
    std::scoped_lock lock(survivingMap->mutex);
    assert(survivingMap->values.at("alive")->roots.at(0).integer == 1);
  }
  lua_getfield(state, 1, "self");
  assert(lua_rawequal(state, 1, -1));
  lua_settop(state, 0);
  const int cancelledCount = cancelled->pushResult(state);
  assert(cancelledCount == 2);
  assert(lua_isnil(state, 1) &&
         std::string(lua_tostring(state, 2)) == "cancelled");
  lua_close(state);
}
} // namespace

int main() {
  checkParallelWorkersAndMainInput();
  checkSnapshotsAndCaptureValidation();
  checkErrorsAndResults();
  checkWorkerReuseAndIsolation();
  checkQueuedAndCooperativeCancellation();
  checkRunningCancellationSettlesAfterExecution();
  checkSceneCancellationAndShutdown();
  checkHandleLifetimeAndConcurrentReaders();
  std::cout << "Lua worker task checks passed\n";
}
