#include "demi/runtime/network/NetworkSystem.h"

#include <algorithm>
#include <cstddef>
#include <mutex>

#if DEMI_HAS_ENET
#include <enet/enet.h>
#endif

namespace demi::runtime {

namespace {

#if DEMI_HAS_ENET

std::mutex enetMutex;
int enetRefCount = 0;

[[nodiscard]] std::uint32_t peerId(const ENetPeer* peer) {
  return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(peer != nullptr ? peer->data : nullptr));
}

void setPeerId(ENetPeer* peer, const std::uint32_t id) {
  if (peer != nullptr) {
    peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
  }
}

[[nodiscard]] ENetPacket* makePacket(const std::string& message, const bool reliable) {
  const enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
  return enet_packet_create(message.data(), message.size(), flags);
}

#endif

} // namespace

NetworkSystem::NetworkSystem() = default;

NetworkSystem::~NetworkSystem() {
  shutdown();
}

bool NetworkSystem::initialize() {
#if !DEMI_HAS_ENET
  return false;
#else
  if (initialized_) {
    return true;
  }

  std::scoped_lock lock(enetMutex);
  if (enetRefCount == 0 && enet_initialize() != 0) {
    return false;
  }
  ++enetRefCount;
  initialized_ = true;
  return true;
#endif
}

bool NetworkSystem::available() const {
#if DEMI_HAS_ENET
  return true;
#else
  return false;
#endif
}

bool NetworkSystem::host(const std::uint16_t port, const std::uint32_t maxPeers, const std::uint8_t channels) {
#if !DEMI_HAS_ENET
  (void)port;
  (void)maxPeers;
  (void)channels;
  return false;
#else
  if (!initialize()) {
    return false;
  }
  disconnect();

  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = port;
  host_ = enet_host_create(&address, static_cast<std::size_t>(std::max<std::uint32_t>(maxPeers, 1)), std::max<std::uint8_t>(channels, 1), 0, 0);
  if (host_ == nullptr) {
    return false;
  }
  mode_ = NetworkMode::Host;
  connected_ = true;
  return true;
#endif
}

bool NetworkSystem::connect(const std::string& addressText, const std::uint16_t port, const std::uint8_t channels) {
#if !DEMI_HAS_ENET
  (void)addressText;
  (void)port;
  (void)channels;
  return false;
#else
  if (!initialize()) {
    return false;
  }
  disconnect();

  host_ = enet_host_create(nullptr, 1, std::max<std::uint8_t>(channels, 1), 0, 0);
  if (host_ == nullptr) {
    return false;
  }

  ENetAddress address{};
  if (enet_address_set_host(&address, addressText.c_str()) != 0) {
    disconnect();
    return false;
  }
  address.port = port;
  serverPeer_ = enet_host_connect(static_cast<ENetHost*>(host_), &address, std::max<std::uint8_t>(channels, 1), 0);
  if (serverPeer_ == nullptr) {
    disconnect();
    return false;
  }
  enet_peer_timeout(static_cast<ENetPeer*>(serverPeer_), 0, 1000, 3000);
  mode_ = NetworkMode::Client;
  connected_ = false;
  return true;
#endif
}

void NetworkSystem::disconnect() {
#if DEMI_HAS_ENET
  if (host_ != nullptr) {
    auto* enetHost = static_cast<ENetHost*>(host_);
    if (mode_ == NetworkMode::Client && serverPeer_ != nullptr) {
      enet_peer_disconnect_now(static_cast<ENetPeer*>(serverPeer_), 0);
    } else if (mode_ == NetworkMode::Host) {
      for (auto& [_, peer] : peers_) {
        if (peer != nullptr) {
          enet_peer_disconnect_now(static_cast<ENetPeer*>(peer), 0);
        }
      }
    }
    enet_host_destroy(enetHost);
  }
#endif
  host_ = nullptr;
  serverPeer_ = nullptr;
  peers_.clear();
  events_.clear();
  mode_ = NetworkMode::Offline;
  connected_ = false;
  nextPeerId_ = 1;
}

bool NetworkSystem::send(const std::string& message, const bool reliable, const std::uint8_t channel, const std::uint32_t peerIdValue) {
#if !DEMI_HAS_ENET
  (void)message;
  (void)reliable;
  (void)channel;
  (void)peerIdValue;
  return false;
#else
  if (host_ == nullptr || message.empty()) {
    return false;
  }

  if (mode_ == NetworkMode::Client) {
    if (serverPeer_ == nullptr) {
      return false;
    }
    ENetPacket* packet = makePacket(message, reliable);
    if (packet == nullptr || enet_peer_send(static_cast<ENetPeer*>(serverPeer_), channel, packet) != 0) {
      if (packet != nullptr) {
        enet_packet_destroy(packet);
      }
      return false;
    }
    enet_host_flush(static_cast<ENetHost*>(host_));
    return true;
  }

  if (mode_ == NetworkMode::Host) {
    ENetPacket* packet = makePacket(message, reliable);
    if (packet == nullptr) {
      return false;
    }
    if (peerIdValue == 0) {
      enet_host_broadcast(static_cast<ENetHost*>(host_), channel, packet);
      enet_host_flush(static_cast<ENetHost*>(host_));
      return true;
    }
    const auto found = peers_.find(peerIdValue);
    if (found == peers_.end() || found->second == nullptr || enet_peer_send(static_cast<ENetPeer*>(found->second), channel, packet) != 0) {
      enet_packet_destroy(packet);
      return false;
    }
    enet_host_flush(static_cast<ENetHost*>(host_));
    return true;
  }
  return false;
#endif
}

void NetworkSystem::update(const std::uint32_t maxEvents) {
#if DEMI_HAS_ENET
  if (host_ == nullptr) {
    return;
  }

  auto* enetHost = static_cast<ENetHost*>(host_);
  for (std::uint32_t i = 0; i < maxEvents; ++i) {
    ENetEvent event{};
    const int result = enet_host_service(enetHost, &event, 0);
    if (result <= 0) {
      break;
    }

    if (event.type == ENET_EVENT_TYPE_CONNECT) {
      std::uint32_t id = 0;
      if (mode_ == NetworkMode::Host) {
        id = nextPeerId_++;
        setPeerId(event.peer, id);
        peers_[id] = event.peer;
      } else {
        id = 0;
        serverPeer_ = event.peer;
        connected_ = true;
      }
      events_.push_back(NetworkEvent{.type = NetworkEventType::Connected, .peerId = id, .channel = 0, .message = {}, .latencyMs = event.peer != nullptr ? event.peer->roundTripTime : 0});
    } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
      const std::uint32_t id = mode_ == NetworkMode::Host ? peerId(event.peer) : 0;
      const auto* bytes = static_cast<const char*>(static_cast<const void*>(event.packet->data));
      events_.push_back(NetworkEvent{.type = NetworkEventType::Message, .peerId = id, .channel = event.channelID, .message = std::string(bytes, bytes + event.packet->dataLength), .latencyMs = event.peer != nullptr ? event.peer->roundTripTime : 0});
      enet_packet_destroy(event.packet);
    } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
      const std::uint32_t id = mode_ == NetworkMode::Host ? peerId(event.peer) : 0;
      if (mode_ == NetworkMode::Host) {
        peers_.erase(id);
      } else {
        connected_ = false;
        serverPeer_ = nullptr;
      }
      if (event.peer != nullptr) {
        event.peer->data = nullptr;
      }
      events_.push_back(NetworkEvent{.type = NetworkEventType::Disconnected, .peerId = id, .channel = 0, .message = {}, .latencyMs = 0});
    }
  }
#else
  (void)maxEvents;
#endif
}

std::vector<NetworkEvent> NetworkSystem::drainEvents() {
  std::vector<NetworkEvent> events = std::move(events_);
  events_.clear();
  return events;
}

NetworkMode NetworkSystem::mode() const {
  return mode_;
}

bool NetworkSystem::isHosting() const {
  return mode_ == NetworkMode::Host;
}

bool NetworkSystem::isConnected() const {
  return connected_;
}

std::uint32_t NetworkSystem::latencyMs() const {
#if DEMI_HAS_ENET
  if (mode_ == NetworkMode::Client && serverPeer_ != nullptr) {
    return static_cast<ENetPeer*>(serverPeer_)->roundTripTime;
  }
  if (mode_ == NetworkMode::Host && !peers_.empty() && peers_.begin()->second != nullptr) {
    return static_cast<ENetPeer*>(peers_.begin()->second)->roundTripTime;
  }
#endif
  return 0;
}

void NetworkSystem::shutdown() {
  disconnect();
#if DEMI_HAS_ENET
  if (initialized_) {
    std::scoped_lock lock(enetMutex);
    --enetRefCount;
    if (enetRefCount == 0) {
      enet_deinitialize();
    }
  }
#endif
  initialized_ = false;
}

} // namespace demi::runtime
