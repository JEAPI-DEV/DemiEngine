#include "demi/runtime/network/GameNetworkSession.h"
#include "demi/runtime/network/NetworkSystem.h"

#include <chrono>
#include <iostream>
#include <set>
#include <thread>

namespace {

void pump(demi::runtime::NetworkSystem &host,
          demi::runtime::NetworkSystem &first,
          demi::runtime::NetworkSystem &second) {
  host.update();
  first.update();
  second.update();
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

} // namespace

int main() {
  using namespace demi::runtime;
  NetworkSystem host;
  NetworkSystem first;
  NetworkSystem second;
  GameNetworkSession session;
  session.reset(true);
  constexpr std::uint16_t Port = 39424;

  if (!host.host(Port, 2) || !first.connect("127.0.0.1", Port) ||
      !second.connect("127.0.0.1", Port)) {
    std::cerr << "Failed to start repeatable two-client session.\n";
    return 1;
  }

  std::set<std::uint32_t> peers;
  for (int attempt = 0; attempt < 500 && peers.size() != 2; ++attempt) {
    pump(host, first, second);
    for (const NetworkEvent &event : host.drainEvents()) {
      if (event.type == NetworkEventType::Connected) {
        peers.insert(event.peerId);
        session.peerConnected(event.peerId);
      }
    }
    (void)first.drainEvents();
    (void)second.drainEvents();
  }
  if (peers.size() != 2 || session.diagnostics().connectedPeers != 2) {
    std::cerr << "Host did not observe two distinct connected clients.\n";
    return 1;
  }

  if (!first.send("spawn:first", true) ||
      !second.send("spawn:second", true)) {
    std::cerr << "Clients failed to send game-facing spawn messages.\n";
    return 1;
  }
  std::set<std::string> messages;
  for (int attempt = 0; attempt < 250 && messages.size() != 2; ++attempt) {
    pump(host, first, second);
    for (const NetworkEvent &event : host.drainEvents()) {
      if (event.type == NetworkEventType::Message) {
        messages.insert(event.message);
        const std::string owner = session.peerName(event.peerId);
        if (!session.registerEntity(event.message, owner)) {
          std::cerr << "Host failed to assign entity authority.\n";
          return 1;
        }
      }
    }
    (void)first.drainEvents();
    (void)second.drainEvents();
  }
  if (messages.size() != 2 ||
      session.owner("spawn:first") == session.owner("spawn:second")) {
    std::cerr << "Two-client state was not kept distinct.\n";
    return 1;
  }

  const auto firstPeer = *session.owner("spawn:first");
  if (session.acceptsStateFrom("spawn:first",
                               *session.owner("spawn:second")) ||
      !session.acceptsStateFrom("spawn:first", firstPeer)) {
    std::cerr << "Cross-client authority spoof was not rejected.\n";
    return 1;
  }

  first.disconnect();
  second.disconnect();
  host.disconnect();
  return 0;
}
