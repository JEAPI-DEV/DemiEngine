#include "demi/runtime/concurrency/AsyncWorkQueue.h"
#include "demi/runtime/database/DatabaseService.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scripting/LuaWorkerContext.h"
#include "demi/runtime/scripting/bindings/LuaDatabaseBindings.h"

#include <sol/sol.hpp>

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void execute(demi::runtime::LuaScriptHost &host, const char *source) {
  const auto result = host.executeConsole(source);
  if (!result.succeeded)
    throw std::runtime_error(result.error);
}

void standaloneInstallation() {
  demi::runtime::DatabaseService service;
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::string);
  demi::runtime::installLuaDatabaseBindings(lua.lua_state(), service, {});
  const auto result = lua.safe_script(R"lua(
    local db, err = Database.open({path = ':memory:'})
    assert(db == nil and err:find('only available in workers'))
    db, err = Database.connect({path = ':memory:'})
    assert(db and err == nil)
    local value, reason = db:connect_operation():wait()
    assert(value == nil and reason:find('only available in workers'))
    assert(type(db:connect_operation():done()) == 'boolean')
  )lua",
                                      sol::script_pass_on_error);
  if (!result.valid())
    throw std::runtime_error(sol::error(result).what());
}

void workerWaitOutcomes() {
  using namespace demi::runtime;
  DatabaseService service;
  AsyncWorkQueue queue(1);
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::string);
  installLuaDatabaseBindings(lua.lua_state(), service, {});
  std::promise<void> release;
  auto released = release.get_future().share();
  auto operation = queue.submit([released](const auto &) {
    released.wait();
    return nlohmann::json{{"connected", true}};
  });
  lua["pending"] = operation;
  std::stop_source cancellation;
  setLuaWorkerContext(lua.lua_state(), cancellation.get_token());
  const auto check = [&](const char *source) {
    const auto result = lua.safe_script(source, sol::script_pass_on_error);
    if (!result.valid())
      throw std::runtime_error(sol::error(result).what());
  };
  try {
    check("local value, err = pending:wait(0); assert(value == nil and err == "
          "'timeout')");
    cancellation.request_stop();
    check("local value, err = pending:wait(); assert(value == nil and err == "
          "'cancelled')");
    require(!operation->cancelled(),
            "Waiting must not cancel the database operation");
    release.set_value();
  } catch (...) {
    release.set_value();
    throw;
  }
  queue.shutdown();
  // Shutdown cancels outstanding work, so use a fresh completed operation for
  // successful repeated waits with the same decoding as result().
  AsyncWorkQueue completedQueue(1);
  operation = completedQueue.submit(
      [](const auto &) { return nlohmann::json{{"connected", true}}; });
  lua["completed"] = operation;
  setLuaWorkerContext(lua.lua_state(), {});
  check("local value, err = completed:wait(1000); assert(value.connected and "
        "err == nil); assert(completed:wait(0).connected)");
  clearLuaWorkerContext(lua.lua_state());
}

void workerDatabaseFromLua() {
  using namespace std::chrono_literals;
  demi::runtime::World world;
  demi::runtime::InputState input;
  demi::runtime::LuaScriptHost host;
  std::string error;
  require(host.initialize(world, input, nullptr, error),
          "Lua worker database host initialization failed");
  execute(host, R"lua(
    local Task = require('demi.task')
    worker_database = Task.fork(function()
      local Database = require('demi.database')
      local db, err = Database.open({path = ':memory:'})
      assert(db, err)
      assert(db:connect_operation():wait().connected)
      local result, reason = db:execute('CREATE TABLE records(value BLOB)'):wait()
      assert(result and result.changes == 0 and reason == nil, reason)
      local bytes = string.char(0, 255, 65)
      assert(db:execute('INSERT INTO records VALUES (?)', {Database.blob(bytes)}):wait())
      result, reason = db:query('SELECT value FROM records'):wait(1000)
      assert(result and result.rows[1][1] == bytes and reason == nil, reason)
      result, reason = db:transaction({{sql = 'SELECT value FROM records', query = true}}):wait()
      assert(result and result[1].rows[1][1] == bytes and reason == nil, reason)
      result, reason = db:query('SELECT value FROM records'):wait(-1)
      assert(result == nil and reason:find('non%-negative'))
      result, reason = db:query('SELECT missing FROM records'):wait()
      assert(result == nil and type(reason) == 'string' and reason ~= '')
      assert(db:close():wait().closed)
      local bad
      bad, reason = Database.open({path = '../outside.db'})
      assert(bad == nil and type(reason) == 'string')
      bad, reason = Database.open({driver = 'missing'})
      assert(bad == nil and reason:find('unknown database driver'))
      bad, reason = Database.open({path = ':memory:', options = {busy_timeout_ms = -1}})
      assert(bad == nil and reason:find('busy_timeout_ms'))
      return true
    end)
  )lua");
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto probe = host.executeConsole("return worker_database:done()");
    require(probe.succeeded, probe.error.c_str());
    if (!probe.values.empty() && probe.values.front() == "true")
      break;
    std::this_thread::sleep_for(1ms);
  }
  execute(host, "assert(worker_database:done()); "
                "assert(worker_database:result(), worker_database:error())");
}

void databaseFromLua() {
  using namespace std::chrono_literals;
  demi::runtime::World world;
  demi::runtime::InputState input;
  demi::runtime::LuaScriptHost host;
  std::string error;
  require(host.initialize(world, input, nullptr, error),
          "Lua database host initialization failed");

  execute(host, R"lua(
    local Database = require('demi.database')
    local Task = require('demi.task')
    local Data = require('demi.data')
    local bad, reason = Database.connect({driver = 'missing', path = ':memory:'})
    assert(bad == nil and type(reason) == 'string' and reason:find('unknown database driver'))
    bad, reason = Database.connect({driver = 'sqlite', path = '../outside.db'})
    assert(bad == nil and type(reason) == 'string')
    bad, reason = Database.connect({driver = 'sqlite', path = '/tmp/outside.db'})
    assert(bad == nil and type(reason) == 'string')

    local connection, connect_error = Database.connect({driver = 'sqlite', path = ':memory:'})
    assert(connection and connect_error == nil)
    busy = Task.fork(function()
      local Task = require('demi.task')
      while not Task.cancelled() do end
    end)
    database_test_coroutine = coroutine.create(function()
      local function settled(operation)
        assert(operation.completion == nil)
        repeat coroutine.yield() until operation:done()
        assert(operation:done() and operation:error() == '', operation:error())
        return operation:result()
      end
      assert(settled(connection:connect_operation()).connected)
      settled(connection:execute('CREATE TABLE records (name TEXT, note TEXT, payload BLOB)'))
      local bytes = string.char(0, 255, 65, 0)
      local blob = Database.blob(bytes)
      assert(type(blob) == 'userdata')
      local types = settled(connection:query('SELECT typeof(?), typeof(?)',
          {bytes, blob}))
      assert(types.rows[1][1] == 'text' and types.rows[1][2] == 'blob')
      local inserted = settled(connection:execute(
          'INSERT INTO records (name, note, payload) VALUES (?, ?, ?)',
          {"O'Reilly", Data.null, blob}))
      assert(inserted.changes == 1)
      local result = settled(connection:query(
          'SELECT name, note, payload, typeof(payload) FROM records WHERE name = ?',
          {"O'Reilly"}))
      assert(Data.kind(result.rows) == 'array' and #result.rows == 1)
      assert(result.rows[1][1] == "O'Reilly" and Data.is_null(result.rows[1][2]))
      assert(result.rows[1][3] == bytes and #result.rows[1][3] == 4)
      assert(result.rows[1][4] == 'blob')
      assert(Data.kind(result.rows[1]) == 'array')
      local batch = settled(connection:transaction({
        {sql = 'INSERT INTO records (name, note, payload) VALUES (?, ?, ?)',
         parameters = {'empty', Data.null, Database.blob('')}},
        {sql = 'SELECT payload, typeof(payload), length(payload) FROM records WHERE name = ?',
         parameters = {'empty'}, query = true}
      }))
      assert(Data.kind(batch) == 'array' and batch[2].rows[1][1] == '')
      assert(batch[2].rows[1][2] == 'blob' and batch[2].rows[1][3] == 0)
      local rejected = connection:query('SELECT ?, ?', {1})
      repeat coroutine.yield() until rejected:done()
      assert(rejected:done() and rejected:result() == nil and rejected:error() ~= '')
      assert(settled(connection:close()).closed)
      database_test_passed = true
    end)
    function poll_database()
      local ok, reason = coroutine.resume(database_test_coroutine)
      assert(ok, reason)
      return coroutine.status(database_test_coroutine) == 'dead'
    end
  )lua");

  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    host.beginFrame(0.016f);
    host.update(0.016f);
    const auto probe = host.executeConsole("return poll_database()");
    require(probe.succeeded, probe.error.c_str());
    if (!probe.values.empty() && probe.values.front() == "true")
      break;
    std::this_thread::sleep_for(1ms);
  }
  execute(host, R"lua(
    assert(database_test_passed)
    assert(not busy:done())
    busy:cancel()
  )lua");
}

} // namespace

int main() {
  try {
    standaloneInstallation();
    workerWaitOutcomes();
    databaseFromLua();
    workerDatabaseFromLua();
    std::cout << "PASS Lua database bindings\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL Lua database bindings: " << error.what() << '\n';
    return 1;
  }
}
