#include "demi/runtime/concurrency/AsyncCompletion.h"
#include "demi/runtime/network/TcpClient.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace std::chrono_literals;
using demi::runtime::TcpClient;
using demi::runtime::TcpOperation;
using demi::runtime::TcpResponse;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

TcpResponse await(const std::shared_ptr<TcpOperation> &operation,
                  std::chrono::milliseconds limit = 3s) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  while (!operation->completion()->ready() &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  require(operation->completion()->ready(), "TCP operation never completed");
  auto result = operation->response();
  require(result != nullptr, "completion preceded response");
  return *result;
}

class Server {
public:
  template <class Handler> explicit Server(Handler handler) {
    listener_ = socket(AF_INET, SOCK_STREAM, 0);
    require(listener_ >= 0, "server socket failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    require(bind(listener_, reinterpret_cast<sockaddr *>(&address),
                 sizeof(address)) == 0,
            "server bind failed");
    require(listen(listener_, 1) == 0, "server listen failed");
    socklen_t size = sizeof(address);
    require(getsockname(listener_, reinterpret_cast<sockaddr *>(&address),
                        &size) == 0,
            "server port lookup failed");
    port_ = ntohs(address.sin_port);
    worker_ = std::thread([this, handler = std::move(handler)] {
      const int peer = accept(listener_, nullptr, nullptr);
      if (peer >= 0) {
        handler(peer);
        ::close(peer);
      }
    });
  }

  ~Server() {
    shutdown(listener_, SHUT_RDWR);
    ::close(listener_);
    worker_.join();
  }

  std::uint16_t port() const { return port_; }

private:
  int listener_ = -1;
  std::uint16_t port_ = 0;
  std::thread worker_;
};

void testBinaryAndDelayedRead() {
  const std::string payload("a\0b\xff", 4);
  Server server([&](int peer) {
    char bytes[4]{};
    require(recv(peer, bytes, sizeof(bytes), MSG_WAITALL) == 4,
            "server did not get binary write");
    require(std::string(bytes, sizeof(bytes)) == payload,
            "write changed binary bytes");
    std::this_thread::sleep_for(100ms);
    require(send(peer, payload.data(), payload.size(), 0) == 4,
            "server reply failed");
  });
  TcpClient client;
  const auto start = std::chrono::steady_clock::now();
  auto connect = client.connect("127.0.0.1", server.port(), 1000);
  require(std::chrono::steady_clock::now() - start < 50ms,
          "connect blocked main thread");
  auto connected = await(connect);
  require(connected.ok && connected.connection && connected.connection->open(),
          "connect failed");
  auto connection = connected.connection;
  require(await(connection->write(payload, 1000)).ok, "binary write failed");
  auto read = connection->read(2, 1000);
  auto duplicate = connection->read(2, 1000);
  require(await(duplicate).errorCode == "read_pending",
          "second pending read was accepted");
  require(await(read).data == payload.substr(0, 2), "first binary read failed");
  require(await(connection->read(2, 1000)).data == payload.substr(2),
          "buffered binary read failed");
  require(await(connection->read(1, 1000)).errorCode == "eof",
          "EOF not reported");
  client.shutdown();
  require(!connection->open(), "connection remained open after shutdown");
}

void testImmutableResponseSnapshots() {
  static_assert(
      std::is_same_v<decltype(std::declval<TcpOperation>().response()),
                     std::shared_ptr<const TcpResponse>>);
  std::string payload(1024U * 1024U, '\0');
  for (std::size_t index = 0; index < payload.size(); ++index) {
    payload[index] = static_cast<char>(index & 0xffU);
  }
  std::atomic<bool> release = false;
  std::atomic<bool> sendFailed = false;
  Server server([&](int peer) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!release.load() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(1ms);
    }
    std::size_t sent = 0;
    while (sent < payload.size()) {
      const auto count = send(peer, payload.data() + sent,
                              payload.size() - sent, MSG_NOSIGNAL);
      if (count <= 0) {
        sendFailed = true;
        return;
      }
      sent += static_cast<std::size_t>(count);
    }
  });
  std::vector<std::shared_ptr<const TcpResponse>> retained;
  {
    TcpClient client;
    auto connection =
        await(client.connect("127.0.0.1", server.port(), 1000)).connection;
    require(connection != nullptr, "snapshot fixture connect failed");
    auto operation = connection->read(payload.size(), 2000);
    require(!operation->response(), "pending TCP read published a response");
    const auto callbackSawResponse = std::make_shared<std::atomic<bool>>(false);
    auto subscription =
        operation->completion()->subscribe([callbackSawResponse, first = operation] {
          const auto response = first->response();
          callbackSawResponse->store(
              response && response->ok && first->completion()->ready());
        });
    release = true;
    std::size_t received = 0;
    while (received < payload.size()) {
      require(operation->completion()->wait({}, 3s) ==
                  demi::runtime::AsyncWaitResult::Ready,
              "large TCP operation did not complete");
      auto snapshot = operation->response();
      require(snapshot && snapshot->ok && !snapshot->data.empty(),
              "large TCP read failed");
      const auto again = operation->response();
      require(again == snapshot && again->data.data() == snapshot->data.data(),
              "TCP response copied its completed payload");
      operation->cancel();
      require(operation->response() == snapshot,
              "late cancellation replaced a completed TCP snapshot");
      received += snapshot->data.size();
      retained.push_back(std::move(snapshot));
      if (received < payload.size()) {
        operation = connection->read(payload.size() - received, 2000);
      }
    }
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!callbackSawResponse->load() &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(1ms);
    }
    require(callbackSawResponse->load(),
            "TCP completion callback could not inspect its response");
    client.shutdown();
    operation.reset();
    connection.reset();
  }
  std::string restored;
  for (const auto &snapshot : retained) {
    restored += snapshot->data;
  }
  require(!sendFailed && restored == payload,
          "TCP snapshots did not retain binary bytes after shutdown");
}

void testTimeoutAndCancellation() {
  Server server([](int peer) {
    std::this_thread::sleep_for(80ms);
    const char data[] = {'x', '\0', 'y'};
    (void)send(peer, data, sizeof(data), 0);
    char byte;
    // Keep the socket open until the client explicitly closes it.
    (void)recv(peer, &byte, 1, 0);
  });
  TcpClient client;
  auto connection =
      await(client.connect("localhost", server.port(), 1000)).connection;
  require(connection != nullptr, "localhost DNS/connect failed");
  require(await(connection->read(32, 30)).errorCode == "timeout",
          "read timeout failed");
  require(await(connection->read(32, 1000)).data == std::string("x\0y", 3),
          "read timeout lost later bytes");
  auto pending = connection->read(32, 1000);
  pending->cancel();
  require(await(pending).errorCode == "cancelled", "read cancellation failed");
  const auto cancelledSnapshot = pending->response();
  pending->cancel();
  require(cancelledSnapshot && pending->response() == cancelledSnapshot,
          "TCP repeated cancellation replaced its snapshot");
  auto next = connection->read(32, 1000);
  connection->close();
  require(await(next).errorCode == "closed",
          "close did not settle pending read");
  client.shutdown();
}

void testShutdownAndDnsLifecycle() {
  TcpClient client;
  auto pendingDns = client.connect("nonexistent.invalid", 1234, 5000);
  pendingDns->cancel();
  require(await(pendingDns).errorCode == "cancelled",
          "DNS cancellation was not immediate");
  client.shutdown();
  require(await(client.connect("localhost", 1234, 100)).errorCode == "shutdown",
          "new connect accepted after shutdown");
  // Repeated shutdown and operations outliving their service are safe.
  client.shutdown();
  require(pendingDns->completion()->ready(),
          "completion invalid after shutdown");
}

void testBufferedByteCap() {
  Server server([](int peer) {
    const char bytes[] = "abcdefgh";
    (void)send(peer, bytes, sizeof(bytes) - 1, 0);
  });
  TcpClient client(TcpClient::Options{.maxBufferedBytes = 2});
  auto connection =
      await(client.connect("127.0.0.1", server.port(), 1000)).connection;
  require(connection != nullptr, "capped connection failed");
  std::string received;
  while (received.size() < 8) {
    const auto chunk = await(connection->read(8, 1000));
    require(chunk.ok && !chunk.data.empty() && chunk.data.size() <= 2,
            "read violated receive buffer cap");
    received += chunk.data;
  }
  require(received == "abcdefgh", "receive cap truncated stream");
  client.shutdown();
}

void testLastConnectionOwnerClosesSocket() {
  std::atomic<bool> peerSawEof = false;
  Server server([&](int peer) {
    pollfd descriptor{peer, POLLIN, 0};
    if (poll(&descriptor, 1, 2000) > 0) {
      char byte;
      peerSawEof.store(recv(peer, &byte, 1, 0) == 0);
    }
  });
  TcpClient client;
  auto connect = client.connect("127.0.0.1", server.port(), 1000);
  auto connection = await(connect).connection;
  require(connection && connection->open(), "ownership fixture connect failed");
  auto otherOwner = connect->response()->connection;
  auto pendingRead = connection->read(8, 1000);
  connection.reset();
  connect.reset();
  require(otherOwner->open(), "dropping one owner closed shared connection");
  otherOwner.reset();
  require(await(pendingRead).errorCode == "closed",
          "last connection owner did not settle pending read");
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (!peerSawEof.load() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  require(peerSawEof.load(), "last connection owner did not close socket");
  client.shutdown();
}

} // namespace

int main() {
  try {
    testBinaryAndDelayedRead();
    testImmutableResponseSnapshots();
    testTimeoutAndCancellation();
    testShutdownAndDnsLifecycle();
    testBufferedByteCap();
    testLastConnectionOwnerClosesSocket();
    std::cout << "TCP client tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "TCP client tests failed: " << error.what() << '\n';
    return 1;
  }
}
