#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include <chrono>
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
    databaseFromLua();
    std::cout << "PASS Lua database bindings\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL Lua database bindings: " << error.what() << '\n';
    return 1;
  }
}
