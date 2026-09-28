#include "demi/runtime/scripting/LuaScriptHost.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
void execute(demi::runtime::LuaScriptHost &host, const char *script) {
  const auto result = host.executeConsole(script);
  if (!result.succeeded) {
    std::cerr << result.error << '\n';
    std::abort();
  }
}

void waitForTask(demi::runtime::LuaScriptHost &host, const char *name) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    host.beginFrame(1.0F / 60.0F);
    host.update(1.0F / 60.0F);
    const auto probe =
        host.executeConsole(std::string("return ") + name + ":done()");
    if (!probe.succeeded) {
      std::cerr << probe.error << '\n';
      std::abort();
    }
    if (!probe.values.empty() && probe.values.front() == "true")
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::cerr << "Timed out waiting for " << name << '\n';
  std::abort();
}

void databaseWorkerAndMainDispatch(const std::filesystem::path &root) {
  using namespace demi::runtime;
  World world;
  world.hudCanvasSize = {100, 100};
  world.ui.canvasSize = {100, 100};
  ui::UiNode label;
  label.id = "database_result";
  label.type = "text";
  world.ui.nodes.push_back(label);
  InputState input;
  LuaScriptHost host;
  std::string error;
  assert(host.initialize(world, input, nullptr, error));
  ProjectData project;
  project.projectDirectory = root;
  project.name = root.filename().string();
  // Storage configuration happens after binding installation at initialize.
  assert(host.loadWorldScripts(project, world, error));
  execute(host, R"lua(
    local Database = require('demi.database')
    local Task = require('demi.task')
    Task.configure({workers = 1})
    storage_db = assert(Database.connect({path = 'late-storage.sqlite'}))
    storage_query = storage_db:query('SELECT 17')
    database_worker = Task.fork(function()
      local Database = require('demi.database')
      local Data = require('demi.data')
      local Vector2 = require('demi.math.vector2')
      local Vector3 = require('demi.math.vector3')
      local Task = require('demi.task')
      assert(Data.kind(Data.null) == 'null' and Data.is_null(Data.null))
      local parsed, err = Data.parse_yaml('items: [1, null, 3]')
      assert(parsed and err == nil, err and err.message)
      assert(Data.kind(parsed.items) == 'array' and Data.is_null(parsed.items[2]))
      assert(Vector2.length({3, 4}) == 5)
      assert(Vector3.dot({1, 2, 3}, {4, 5, 6}) == 32)
      local db = assert(Database.open({path = ':memory:'}))
      local result, reason = db:query('SELECT ?, ?', {42, Data.null}):wait()
      assert(result and reason == nil, reason)
      assert(Data.kind(result.rows) == 'array' and Data.is_null(result.rows[1][2]))
      local published, null_value = Task.main(function(rows)
        local Hud = require('demi.hud')
        local Data = require('demi.data')
        assert(Data.kind(rows) == 'array' and Data.is_null(rows[1][2]))
        assert(Hud.set_text('database_result', tostring(rows[1][1])))
        return Hud.get_text('database_result'), Data.null
      end, result.rows)
      local handoff_diagnostic =
        'Task.main handoff: published=' .. tostring(published) ..
        ' (type=' .. type(published) .. '), null_value=' .. tostring(null_value) ..
        ' (type=' .. type(null_value) .. ', kind=' .. tostring(Data.kind(null_value)) ..
        ', is_null=' .. tostring(Data.is_null(null_value)) ..
        ', same_as_Data_null=' .. tostring(null_value == Data.null) .. ')'
      assert(published == '42', 'Unexpected HUD text; ' .. handoff_diagnostic)
      assert(Data.is_null(null_value), 'Lost null identity; ' .. handoff_diagnostic)
      assert(db:close():wait())
      Database.open = false
      Data.kind = false
      Vector2.length = false
      Vector3.dot = false
      Task.main = false
      return result.rows, published
    end)
  )lua");
  waitForTask(host, "database_worker");
  waitForTask(host, "storage_query");
  execute(host, R"lua(
    local Data = require('demi.data')
    local rows, published = database_worker:result()
    assert(rows, database_worker:error())
    assert(rows[1][1] == 42 and Data.is_null(rows[1][2]) and published == '42')
    assert(require('demi.hud').get_text('database_result') == '42')
    assert(storage_query:done() and storage_query:result().rows[1][1] == 17)
    storage_close = storage_db:close()
    fresh_worker = require('demi.task').fork(function()
      local Database = require('demi.database')
      local Data = require('demi.data')
      local Vector2 = require('demi.math.vector2')
      local Vector3 = require('demi.math.vector3')
      local Task = require('demi.task')
      assert(type(Database.open) == 'function' and type(Data.kind) == 'function')
      assert(Vector2.length({3, 4}) == 5)
      assert(Vector3.dot({1, 0, 0}, {2, 0, 0}) == 2)
      assert(type(Task.main) == 'function')
      local db = assert(Database.open({path = ':memory:'}))
      assert(db:close():wait())
      return Data.kind(Data.null)
    end)
  )lua");
  waitForTask(host, "fresh_worker");
  execute(host,
          "assert(fresh_worker:result() == 'null', fresh_worker:error())");
  waitForTask(host, "storage_close");
  execute(host, R"lua(
    cancelled_main_ran = false
    cancellation_signal = assert(require('demi.shared').map())
    cancellation_worker = require('demi.task').fork(function(signal)
      local Task = require('demi.task')
      assert(signal:set('calling_main', true))
      return Task.main(function()
        cancelled_main_ran = true
        return true
      end)
    end, cancellation_signal)
  )lua");
  // Do not pump updates: the main callback must remain undispatched until
  // cancellation, including if cancellation races its queue submission.
  const auto cancellationDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  bool callingMain = false;
  while (std::chrono::steady_clock::now() < cancellationDeadline) {
    const auto probe =
        host.executeConsole("return cancellation_signal:get('calling_main')");
    assert(probe.succeeded);
    if (!probe.values.empty() && probe.values.front() == "true") {
      callingMain = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(callingMain);
  execute(host, "cancellation_worker:cancel()");
  waitForTask(host, "cancellation_worker");
  execute(host, "assert(cancellation_worker:status() == 'cancelled'); "
                "assert(not cancelled_main_ran)");
  const auto databaseFile = host.applicationServices().userDataPath() /
                            "databases/late-storage.sqlite";
  host.destroy();
  std::filesystem::remove(databaseFile);
}
} // namespace

int main() {
  using namespace demi::runtime;
  const auto root =
      std::filesystem::temp_directory_path() /
      ("demi-task-host-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  assert(std::filesystem::create_directory(root));
  std::filesystem::create_directory(root / "scripts");
  {
    std::ofstream script(root / "scripts/main.lua");
    script << R"lua(
local Task = require('demi.task')
local Game = {}
function Game:on_start()
  urgent_calls = 0
  update_calls = 0
  busy_task = Task.fork(function()
    local Task = require('demi.task')
    while not Task.cancelled() do
      local sum = 0
      for i = 1, 10000 do sum = sum + i end
    end
  end)
end
-- @HandleAction("urgent")
function Game:on_urgent(event)
  urgent_calls = urgent_calls + 1
end
function Game:on_update(dt)
  update_calls = update_calls + 1
end
return Game
)lua";
  }
  {
    World world;
    world.hudCanvasSize = {100, 100};
    world.ui.canvasSize = {100, 100};
    ui::UiNode button;
    button.id = "urgent_button";
    button.type = "button";
    button.action = "urgent";
    button.layout.size = {100, 100};
    button.resolved = {0, 0, 100, 100};
    button.focusable = true;
    world.ui.nodes.push_back(button);
    InputState input;
    input.mousePosition = {50, 50};
    ProjectData project;
    project.projectDirectory = root;
    project.scriptEntry = "script://scripts/main.lua";
    LuaScriptHost host;
    std::string error;
    assert(host.initialize(world, input, nullptr, error));
    assert(host.loadWorldScripts(project, world, error));
    host.setViewport(100, 100);
    host.start();
    const auto started = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 20; ++frame) {
      if (frame % 2 == 0)
        input.mouseButtonsDown.insert("left");
      else
        input.mouseButtonsDown.clear();
      host.beginFrame(1.0F / 60.0F);
      host.update(1.0F / 60.0F);
    }
    assert(std::chrono::steady_clock::now() - started <
           std::chrono::seconds(2));
    execute(host, "assert(urgent_calls==10); assert(update_calls==20); "
                  "assert(not busy_task:done())");
    host.destroy();
    execute(host, "assert(busy_task:status()=='cancelled')");
  }
  databaseWorkerAndMainDispatch(root);
  std::filesystem::remove_all(root);
  std::cout << "HUD actions stay responsive during worker Lua tasks\n";
}
