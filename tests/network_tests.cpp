#include "demi/runtime/network/NetworkSystem.h"
#include "demi/runtime/network/TlsMessaging.h"

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

bool testDtls() {
  demi::runtime::NetworkSystem server;
  demi::runtime::NetworkSystem client;
  constexpr std::uint16_t Port = 39422;

  if (!server.hostSecure(Port, DEMI_DTLS_TEST_CERT, DEMI_DTLS_TEST_KEY, 2)) {
    std::cerr << "DTLS server failed to start: " << server.securityError() << '\n';
    return false;
  }
  if (!client.connectSecure("127.0.0.1", Port, DEMI_DTLS_TEST_CA, "localhost")) {
    std::cerr << "DTLS client failed to start: " << client.securityError() << '\n';
    return false;
  }

  std::optional<std::uint32_t> serverPeerId;
  bool clientConnected = false;
  for (int attempt = 0; attempt < 500 && (!serverPeerId.has_value() || !clientConnected); ++attempt) {
    pump(server, client);
    for (const demi::runtime::NetworkEvent& event : server.drainEvents()) {
      if (event.type == demi::runtime::NetworkEventType::Connected) {
        serverPeerId = event.peerId;
      }
    }
    for (const demi::runtime::NetworkEvent& event : client.drainEvents()) {
      if (event.type == demi::runtime::NetworkEventType::Connected) {
        clientConnected = true;
      }
    }
  }
  if (!serverPeerId.has_value() || !clientConnected || !server.isSecure() || !client.isSecure()) {
    std::cerr << "DTLS handshake failed: server=" << server.securityError() << " client=" << client.securityError() << '\n';
    return false;
  }

  if (!client.send("encrypted:client", true) || !server.send("encrypted:server", true, 0, *serverPeerId)) {
    std::cerr << "DTLS encrypted send failed.\n";
    return false;
  }

  bool serverReceived = false;
  bool clientReceived = false;
  for (int attempt = 0; attempt < 250 && (!serverReceived || !clientReceived); ++attempt) {
    pump(server, client);
    for (const demi::runtime::NetworkEvent& event : server.drainEvents()) {
      serverReceived = serverReceived || (event.type == demi::runtime::NetworkEventType::Message && event.message == "encrypted:client");
    }
    for (const demi::runtime::NetworkEvent& event : client.drainEvents()) {
      clientReceived = clientReceived || (event.type == demi::runtime::NetworkEventType::Message && event.message == "encrypted:server");
    }
  }
  if (!serverReceived || !clientReceived) {
    std::cerr << "DTLS did not decrypt messages in both directions.\n";
    return false;
  }
  if (client.send(std::string(64 * 1024 + 1, 'x'))) {
    std::cerr << "TLS accepted an oversized frame.\n";
    return false;
  }
  client.disconnect();
  demi::runtime::TlsServer wrongServer;
  if (!wrongServer.listen(Port + 1, DEMI_DTLS_TEST_CERT, DEMI_DTLS_TEST_KEY, 2)) {
    std::cerr << "TLS mismatch test server failed: " << wrongServer.lastError() << '\n';
    return false;
  }
  demi::runtime::TlsClient wrongName;
  if (!wrongName.connect("127.0.0.1", Port + 1, DEMI_DTLS_TEST_CA, "wrong.example")) {
    std::cerr << "TLS mismatched-name client could not start: " << wrongName.lastError() << '\n';
    return false;
  }
  for (int i = 0; i < 250 && !wrongName.lastError().size(); ++i) {
    wrongServer.update(); wrongName.update(); (void)wrongServer.drainEvents(); (void)wrongName.drainEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  if (wrongName.isConnected() || wrongName.lastError().empty()) {
    std::cerr << "TLS accepted a mismatched certificate hostname.\n";
    return false;
  }
  return true;
}

bool testTlsMessaging() {
  demi::runtime::TlsServer server;
  demi::runtime::TlsClient client;
  constexpr std::uint16_t Port = 39423;
  if (!server.listen(Port, DEMI_DTLS_TEST_CERT, DEMI_DTLS_TEST_KEY, 4) ||
      !client.connect("127.0.0.1", Port, DEMI_DTLS_TEST_CA, "localhost")) {
    std::cerr << "TLS messaging setup failed: " << server.lastError() << ' ' << client.lastError() << '\n';
    return false;
  }
  std::optional<std::uint32_t> clientId;
  for (int i = 0; i < 500 && (!clientId.has_value() || !client.isConnected()); ++i) {
    server.update(); client.update();
    for (const auto& event : server.drainEvents()) if (event.type == demi::runtime::TlsEventType::Connected) clientId = event.clientId;
    (void)client.drainEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  if (!clientId.has_value() || !client.isConnected()) {
    std::cerr << "Pinned TLS handshake failed: server=" << server.lastError() << " client=" << client.lastError() << '\n';
    return false;
  }
  if (!client.send("tls:client") || !server.send(*clientId, "tls:server")) return false;
  bool serverReceived = false, clientReceived = false;
  for (int i = 0; i < 250 && (!serverReceived || !clientReceived); ++i) {
    server.update(); client.update();
    for (const auto& event : server.drainEvents()) serverReceived |= event.message == "tls:client";
    for (const auto& event : client.drainEvents()) clientReceived |= event.message == "tls:server";
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  if (!serverReceived || !clientReceived) {
    std::cerr << "TLS framed messages did not round-trip.\n";
    return false;
  }
  return true;
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
  return testDtls() && testTlsMessaging() ? 0 : 1;
}
