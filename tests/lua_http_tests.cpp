#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scripting/LuaWorkerContext.h"
#include "demi/runtime/scripting/bindings/LuaHttpBindings.h"

#include <sol/sol.hpp>

#include <chrono>
#include <iostream>
#include <thread>

int main() {
  {
    demi::runtime::HttpClient client;
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::coroutine, sol::lib::math);
    demi::runtime::installLuaHttpBindings(lua.lua_state(), client);
    demi::runtime::setLuaWorkerContext(lua.lua_state(), {});
    const auto result = lua.safe_script(R"lua(
      local request = assert(Http.get('http://127.0.0.1:1'))
      for _, timeout in ipairs({-1, 1.5, math.huge, 'bad'}) do
        local response, err = request:wait(timeout)
        assert(response == nil and type(err) == 'string')
      end
      request:cancel()
      local co = coroutine.create(function()
        local response, err = request:wait()
        assert(response and err == nil)
        assert(not response.ok and response.error ~= '')
        assert(response.error_code == request:response().error_code)
        assert(request:wait(0).error_code == response.error_code)
      end)
      local ok, err = coroutine.resume(co)
      assert(ok, err)
    )lua",
                                        sol::script_pass_on_error);
    if (!result.valid()) {
      const sol::error failure = result;
      std::cerr << failure.what() << '\n';
      return 1;
    }
    demi::runtime::clearLuaWorkerContext(lua.lua_state());
  }
  demi::runtime::World world;
  demi::runtime::InputState input;
  demi::runtime::LuaScriptHost host;
  std::string error;
  if (!host.initialize(world, input, nullptr, error)) {
    std::cerr << error << '\n';
    return 1;
  }
  const auto validation = host.executeConsole(R"lua(
    local Http = require("demi.network.http")
    local Network = require("demi.network")
    assert(Network.http_get == nil and Network.lobby_list == nil)
    assert(HttpRequestHandle == nil)
    local function rejects(options)
      local request, error = Http.request(options)
      assert(request == nil and type(error) == "string" and #error > 0)
    end
    rejects({ url = "file:///etc/passwd" })
    rejects({ url = "http://user:password@localhost" })
    rejects({ url = "http://localhost", timeout_ms = -1 })
    rejects({ url = "http://localhost", timeout_ms = 1.5 })
    rejects({ url = "http://localhost", timeout_ms = math.huge })
    rejects({ url = "http://localhost", max_response_bytes = "large" })
    rejects({ url = "http://localhost", unknown_option = true })
    rejects({ url = "http://localhost", headers = { Accept = 123 } })
    rejects({ url = "http://localhost", headers = { ["X-Probe"] = "a\r\nb" } })
    rejects({ url = "http://localhost", body = "raw", json = {} })
    local cycle = {}; cycle.self = cycle
    rejects({ url = "http://localhost", json = cycle })
    rejects({ url = "http://localhost", json = { number = math.huge } })
    rejects({ url = "http://localhost", json = { callback = function() end } })

    local options = { timeout_ms = 1000, json = { score = 5 } }
    local request, error = Http.post("http://127.0.0.1:1", options)
    assert(request ~= nil and error == nil)
    assert(options.method == nil and options.url == nil and options.body == nil)
    assert(type(request.done) == "function" and type(request.response) == "function")
    local response, wait_error = request:wait(0)
    assert(response == nil and wait_error:find("workers"))
    request:cancel()
    response, wait_error = request:wait()
    assert(response == nil and wait_error:find("workers"))
    http_request_under_test = request
  )lua");
  if (!validation.succeeded) {
    std::cerr << validation.error << '\n';
    return 1;
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  bool completed = false;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto probe =
        host.executeConsole("return http_request_under_test:done()");
    if (!probe.succeeded) {
      std::cerr << probe.error << '\n';
      return 1;
    }
    if (!probe.values.empty() && probe.values.front() == "true") {
      completed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!completed) {
    std::cerr << "HTTP cancellation did not complete within five seconds\n";
    return 1;
  }
  const auto response = host.executeConsole(R"lua(
    local response = http_request_under_test:response()
    assert(type(response) == "table" and response.ok == false)
    assert(type(response.status) == "number")
    assert(type(response.body) == "string" and type(response.headers) == "table")
    assert(response.error ~= "" and response.error_code ~= "")
    assert(type(response.json_error) == "string")
    assert(http_request_under_test:response().error_code == response.error_code)
  )lua");
  if (!response.succeeded) {
    std::cerr << response.error << '\n';
    return 1;
  }
  host.destroy();
  return 0;
}
