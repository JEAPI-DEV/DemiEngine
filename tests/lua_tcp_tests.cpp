#include "demi/runtime/scripting/LuaScriptHost.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

class LoopbackServer {
public:
  LoopbackServer() {
    listener_ = socket(AF_INET, SOCK_STREAM, 0);
    require(listener_ >= 0, "TCP fixture socket failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(bind(listener_, reinterpret_cast<sockaddr *>(&address),
                 sizeof(address)) == 0,
            "TCP fixture bind failed");
    require(listen(listener_, 1) == 0, "TCP fixture listen failed");
    socklen_t length = sizeof(address);
    require(getsockname(listener_, reinterpret_cast<sockaddr *>(&address),
                        &length) == 0,
            "TCP fixture port lookup failed");
    port_ = ntohs(address.sin_port);
    worker_ = std::thread([this] {
      const int peer = accept(listener_, nullptr, nullptr);
      if (peer < 0)
        return;
      timeval receiveTimeout{.tv_sec = 3, .tv_usec = 0};
      (void)setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &receiveTimeout,
                       sizeof(receiveTimeout));
      char bytes[4]{};
      const auto size = recv(peer, bytes, sizeof(bytes), MSG_WAITALL);
      receivedBinary_.store(size == 4 && std::string(bytes, 4) ==
                                             std::string("a\0b\xff", 4));
      if (size == 4) {
        std::this_thread::sleep_for(200ms);
        const char reply[] = {'x', '\0', 'y', static_cast<char>(0xff)};
        sentReply_.store(send(peer, reply, sizeof(reply), MSG_NOSIGNAL) == 4);
      }
      ::close(peer);
    });
  }

  ~LoopbackServer() {
    shutdown(listener_, SHUT_RDWR);
    ::close(listener_);
    worker_.join();
  }

  [[nodiscard]] std::uint16_t port() const { return port_; }
  [[nodiscard]] bool receivedBinary() const { return receivedBinary_.load(); }
  [[nodiscard]] bool sentReply() const { return sentReply_.load(); }

private:
  int listener_ = -1;
  std::uint16_t port_ = 0;
  std::thread worker_;
  std::atomic<bool> receivedBinary_ = false;
  std::atomic<bool> sentReply_ = false;
};

void execute(demi::runtime::LuaScriptHost &host, const std::string &script) {
  const auto result = host.executeConsole(script);
  if (!result.succeeded)
    throw std::runtime_error(result.error);
}

void awaitLua(demi::runtime::LuaScriptHost &host, const char *expression) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto result =
        host.executeConsole(std::string("return ") + expression);
    require(result.succeeded, "Lua TCP readiness probe failed");
    if (!result.values.empty() && result.values.front() == "true")
      return;
    std::this_thread::sleep_for(1ms);
  }
  throw std::runtime_error("Lua TCP operation did not complete");
}

void testLuaTcp() {
  demi::runtime::World world;
  demi::runtime::InputState input;
  demi::runtime::LuaScriptHost host;
  std::string error;
  require(host.initialize(world, input, nullptr, error),
          "Lua host initialization failed");
  LoopbackServer server;
  execute(host, "TCP_TEST_PORT = " + std::to_string(server.port()));

  execute(host, R"lua(
    local Tcp = require("demi.network.tcp")
    assert(TcpOperation == nil and TcpConnection == nil)
    local function rejects(address, port, options)
      local operation, error = Tcp.connect(address, port, options)
      assert(operation == nil and type(error) == "string" and #error > 0)
    end
    rejects("localhost", 0)
    rejects("localhost", 65536)
    rejects("localhost", 1.5)
    rejects("localhost", math.huge)
    rejects("a\0b", 80)
    rejects("localhost", 80, {timeout_ms = -1})
    rejects("localhost", 80, {timeout_ms = 1.5})
    rejects("localhost", 80, {timeout_ms = math.huge})
    rejects("localhost", 80, {unknown = true})
    local op, error = Tcp.connect("127.0.0.1", TCP_TEST_PORT, {timeout_ms = 1000})
    assert(op ~= nil and error == nil)
    tcp_connect = op
    local co = coroutine.create(function()
      assert(tcp_connect.completion == nil)
      assert(type(tcp_connect:done()) == "boolean")
    end)
    assert(coroutine.resume(co))
  )lua");

  awaitLua(host, "tcp_connect:done()");
  execute(host, R"lua(
    local co = coroutine.create(function()
      local response = tcp_connect:response()
      assert(response.ok and response.error_code == "" and response.data == "")
      assert(response.connection and response.connection:open())
      tcp_connection = response.connection
    end)
    local ok, error = coroutine.resume(co)
    assert(ok, error)
    assert(not pcall(function() tcp_connection:read(0) end))
    assert(not pcall(function() tcp_connection:read(2, {timeout_ms = math.huge}) end))
    assert(not pcall(function() tcp_connection:write(123) end))
    tcp_write = tcp_connection:write("a\0b\255", {timeout_ms = 1000})
  )lua");
  awaitLua(host, "tcp_write:done()");
  execute(host, R"lua(
    assert(tcp_write:response().ok)
    tcp_first_read = tcp_connection:read(2, {timeout_ms = 1000})
    tcp_second_read = tcp_connection:read(2, {timeout_ms = 1000})
  )lua");
  awaitLua(host, "tcp_second_read:done()");
  execute(host, R"lua(
    assert(tcp_second_read:response().error_code == "read_pending")
  )lua");
  awaitLua(host, "tcp_first_read:done()");
  execute(host, R"lua(
    assert(tcp_first_read:response().data == "x\0")
    tcp_tail = tcp_connection:read(8, {timeout_ms = 1000})
  )lua");
  awaitLua(host, "tcp_tail:done()");
  execute(host, R"lua(
    assert(tcp_tail:response().data == "y\255")
    tcp_eof = tcp_connection:read(8, {timeout_ms = 1000})
  )lua");
  awaitLua(host, "tcp_eof:done()");
  execute(host, R"lua(
    assert(tcp_eof:response().error_code == "eof")
    tcp_connection:close()
  )lua");
  require(server.receivedBinary() && server.sentReply(),
          "TCP fixture binary exchange failed");
  host.destroy();
}

void testPollingWithBusyWorker() {
  demi::runtime::World world;
  demi::runtime::InputState input;
  demi::runtime::LuaScriptHost host;
  std::string error;
  require(host.initialize(world, input, nullptr, error), error.c_str());
  LoopbackServer server;
  execute(host, "TCP_TEST_PORT = " + std::to_string(server.port()));
  execute(host, R"lua(
    local Task = require('demi.task')
    busy_task = Task.fork(function()
      local Task = require('demi.task')
      while not Task.cancelled() do end
    end)
    tcp_coroutine = coroutine.create(function()
      local function settled(operation)
        assert(operation.completion == nil)
        repeat coroutine.yield() until operation:done()
        local response = operation:response()
        assert(response.ok, response.error)
        return response
      end
      local op = assert(require('demi.network.tcp').connect('127.0.0.1', TCP_TEST_PORT))
      local response = settled(op)
      local connection = response.connection
      local send = connection:write('a\0b\255')
      settled(send)
      local received = ''
      while #received < 4 do
        local read = connection:read(4 - #received)
        local chunk = settled(read)
        received = received .. chunk.data
      end
      connection:close()
      tcp_received = received
    end)
    function poll_tcp()
      local ok, reason = coroutine.resume(tcp_coroutine)
      assert(ok, reason)
      return coroutine.status(tcp_coroutine) == 'dead'
    end
  )lua");
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  int updates = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    host.beginFrame(0.016F);
    host.update(0.016F);
    ++updates;
    const auto probe = host.executeConsole("return poll_tcp()");
    require(probe.succeeded, probe.error.c_str());
    if (!probe.values.empty() && probe.values.front() == "true")
      break;
    std::this_thread::sleep_for(1ms);
  }
  require(updates > 1, "Delayed TCP exchange did not span updates");
  execute(host, R"lua(
    assert(coroutine.status(tcp_coroutine)=='dead')
    assert(tcp_received=='x\0y\255')
    assert(not busy_task:done())
    busy_task:cancel()
  )lua");
}

} // namespace

int main() {
  try {
    testLuaTcp();
    testPollingWithBusyWorker();
    std::cout << "Lua TCP tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Lua TCP tests failed: " << error.what() << '\n';
    return 1;
  }
}
