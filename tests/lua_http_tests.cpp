#include "demi/runtime/scripting/LuaScriptHost.h"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
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
    request:cancel()
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
