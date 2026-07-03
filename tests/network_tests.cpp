#include "demi/runtime/network/NetworkSystem.h"

#include <chrono>
#include <iostream>
#include <optional>
#include <thread>

namespace {

void pump(demi::runtime::NetworkSystem& server, demi::runtime::NetworkSystem& client, const int iterations = 1) {
  for (int i = 0; i < iterations; ++i) {
    server.update();
    client.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

} // namespace

int main() {
  demi::runtime::NetworkSystem server;
  demi::runtime::NetworkSystem client;

  if (!server.initialize() || !client.initialize()) {
    std::cerr << "ENet networking failed to initialize.\n";
    return 1;
  }

  constexpr std::uint16_t Port = 39421;
  if (!server.host(Port, 2)) {
    std::cerr << "Server failed to host local ENet session.\n";
    return 1;
  }
  if (!client.connect("127.0.0.1", Port)) {
    std::cerr << "Client failed to start local ENet connection.\n";
    return 1;
  }

  std::optional<std::uint32_t> serverPeerId;
  for (int attempt = 0; attempt < 250 && !serverPeerId.has_value(); ++attempt) {
    pump(server, client);
    for (const demi::runtime::NetworkEvent& event : server.drainEvents()) {
      if (event.type == demi::runtime::NetworkEventType::Connected) {
        serverPeerId = event.peerId;
      }
    }
    (void)client.drainEvents();
  }

  if (!serverPeerId.has_value() || !client.isConnected()) {
    std::cerr << "Local ENet peers did not connect.\n";
    return 1;
  }

  if (!client.send("client:hello", true)) {
    std::cerr << "Client failed to send reliable message.\n";
    return 1;
  }

  bool serverReceived = false;
  for (int attempt = 0; attempt < 250 && !serverReceived; ++attempt) {
    pump(server, client);
    for (const demi::runtime::NetworkEvent& event : server.drainEvents()) {
      if (event.type == demi::runtime::NetworkEventType::Message && event.peerId == *serverPeerId && event.message == "client:hello") {
        serverReceived = true;
      }
    }
    (void)client.drainEvents();
  }
  if (!serverReceived) {
    std::cerr << "Server did not receive reliable client message.\n";
    return 1;
  }

  if (!server.send("server:world", true, 0, *serverPeerId)) {
    std::cerr << "Server failed to send reliable peer message.\n";
    return 1;
  }

  bool clientReceived = false;
  for (int attempt = 0; attempt < 250 && !clientReceived; ++attempt) {
    pump(server, client);
    (void)server.drainEvents();
    for (const demi::runtime::NetworkEvent& event : client.drainEvents()) {
      if (event.type == demi::runtime::NetworkEventType::Message && event.message == "server:world") {
        clientReceived = true;
      }
    }
  }
  if (!clientReceived) {
    std::cerr << "Client did not receive reliable server message.\n";
    return 1;
  }

  client.disconnect();
  pump(server, client, 10);

  if (!client.connect("127.0.0.1", Port)) {
    std::cerr << "Client failed to start reconnect to local ENet session.\n";
    return 1;
  }

  std::optional<std::uint32_t> reconnectedPeerId;
  for (int attempt = 0; attempt < 250 && !reconnectedPeerId.has_value(); ++attempt) {
    pump(server, client);
    for (const demi::runtime::NetworkEvent& event : server.drainEvents()) {
      if (event.type == demi::runtime::NetworkEventType::Connected) {
        reconnectedPeerId = event.peerId;
      }
    }
    (void)client.drainEvents();
  }

  if (!reconnectedPeerId.has_value() || !client.isConnected()) {
    std::cerr << "Local ENet client did not reconnect.\n";
    return 1;
  }

  if (!client.send("client:again", true)) {
    std::cerr << "Client failed to send reliable reconnect message.\n";
    return 1;
  }

  bool serverReceivedReconnect = false;
  for (int attempt = 0; attempt < 250 && !serverReceivedReconnect; ++attempt) {
    pump(server, client);
    for (const demi::runtime::NetworkEvent& event : server.drainEvents()) {
      if (event.type == demi::runtime::NetworkEventType::Message && event.peerId == *reconnectedPeerId && event.message == "client:again") {
        serverReceivedReconnect = true;
      }
    }
    (void)client.drainEvents();
  }
  if (!serverReceivedReconnect) {
    std::cerr << "Server did not receive reliable reconnect message.\n";
    return 1;
  }

  client.disconnect();
  server.disconnect();
  return 0;
}
