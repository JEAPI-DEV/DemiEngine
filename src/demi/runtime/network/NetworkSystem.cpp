#include "demi/runtime/network/NetworkSystem.h"

#include "demi/runtime/network/DtlsTransport.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <optional>

#if DEMI_HAS_ENET
#include <enet/enet.h>
#endif

namespace demi::runtime {

#if DEMI_HAS_ENET

struct NetworkSystem::EnetHostHandle::Impl {
  ENetHost *host = nullptr;
};

struct NetworkSystem::EnetPeerHandle::Impl {
  ENetPeer *peer = nullptr;
};

void NetworkSystem::destroyEnetHost(EnetHostHandle *handle) {
  if (handle == nullptr)
    return;
  if (handle->impl != nullptr && handle->impl->host != nullptr)
    enet_host_destroy(handle->impl->host);
  delete handle->impl;
  delete handle;
}

void NetworkSystem::destroyEnetPeer(EnetPeerHandle *handle) {
  if (handle == nullptr)
    return;
  // Peers are owned by their ENetHost; only free our wrapper.
  delete handle->impl;
  delete handle;
}

std::mutex enetMutex;
int enetRefCount = 0;

constexpr std::size_t MaxMessageSize = 64 * 1024;
constexpr std::size_t LengthPrefixSize = 4;

[[nodiscard]] std::uint32_t peerId(const ENetPeer *peer) {
  return static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(peer != nullptr ? peer->data : nullptr));
}

void setPeerId(ENetPeer *peer, const std::uint32_t id) {
  if (peer != nullptr) {
    peer->data = reinterpret_cast<void *>(static_cast<std::uintptr_t>(id));
  }
}

std::string NetworkSystem::frameMessage(const std::string &message) {
  std::string framed;
  framed.reserve(LengthPrefixSize + message.size());
  const std::uint32_t length = static_cast<std::uint32_t>(message.size());
  framed.push_back(static_cast<char>(length & 0xFF));
  framed.push_back(static_cast<char>((length >> 8) & 0xFF));
  framed.push_back(static_cast<char>((length >> 16) & 0xFF));
  framed.push_back(static_cast<char>((length >> 24) & 0xFF));
  framed.append(message);
  return framed;
}

std::optional<std::string>
NetworkSystem::unframeMessage(const char *data, const std::size_t size) {
  if (data == nullptr || size < LengthPrefixSize) {
    return std::nullopt;
  }
  const std::uint32_t length =
      static_cast<std::uint8_t>(data[0]) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[1])) << 8) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[2])) << 16) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[3])) << 24);
  if (length > MaxMessageSize || LengthPrefixSize + length != size) {
    return std::nullopt;
  }
  return std::string(data + LengthPrefixSize, length);
}

[[nodiscard]] ENetPacket *
makePacket(const std::string &framedMessage, const bool reliable) {
  const enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
  return enet_packet_create(framedMessage.data(), framedMessage.size(), flags);
}

#endif

NetworkSystem::NetworkSystem() : dtls_(std::make_unique<DtlsTransport>()) {}

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

bool NetworkSystem::host(const std::uint16_t port,
                         const std::uint32_t maxPeers,
                         const std::uint8_t channels) {
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
  return createHost(port, maxPeers, channels);
#endif
}

bool NetworkSystem::hostSecure(const std::uint16_t port,
                               const std::filesystem::path &certificate,
                               const std::filesystem::path &privateKey,
                               const std::uint32_t maxPeers,
                               const std::uint8_t channels) {
#if !DEMI_HAS_ENET
  (void)port;
  (void)certificate;
  (void)privateKey;
  (void)maxPeers;
  (void)channels;
  return false;
#else
  if (!initialize()) {
    return false;
  }
  disconnect();
  if (!createHost(port, maxPeers, channels)) {
    return false;
  }
  if (!dtls_->configureServer(certificate, privateKey)) {
    std::cerr << "DTLS server init failed: " << dtls_->lastError() << '\n';
    disconnect();
    return false;
  }
  return true;
#endif
}

bool NetworkSystem::connect(const std::string &addressText,
                            const std::uint16_t port,
                            const std::uint8_t channels) {
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
  return createClient(addressText, port, channels);
#endif
}

bool NetworkSystem::connectSecure(const std::string &addressText,
                                  const std::uint16_t port,
                                  const std::filesystem::path &trustedCertificate,
                                  const std::string &serverName,
                                  const std::uint8_t channels) {
#if !DEMI_HAS_ENET
  (void)addressText;
  (void)port;
  (void)trustedCertificate;
  (void)serverName;
  (void)channels;
  return false;
#else
  if (!initialize()) {
    return false;
  }
  disconnect();
  if (!createClient(addressText, port, channels)) {
    return false;
  }
  if (!dtls_->configureClient(trustedCertificate, serverName)) {
    std::cerr << "DTLS client init failed: " << dtls_->lastError() << '\n';
    disconnect();
    return false;
  }
  return true;
#endif
}

bool NetworkSystem::createHost(const std::uint16_t port,
                               const std::uint32_t maxPeers,
                               const std::uint8_t channels) {
#if !DEMI_HAS_ENET
  (void)port;
  (void)maxPeers;
  (void)channels;
  return false;
#else
  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = port;
  auto *rawHost = enet_host_create(
      &address,
      static_cast<std::size_t>(std::max<std::uint32_t>(maxPeers, 1)),
      std::max<std::uint8_t>(channels, 1), 0, 0);
  if (rawHost == nullptr) {
    return false;
  }
  host_ = std::unique_ptr<EnetHostHandle, void(*)(EnetHostHandle*)>{
      new EnetHostHandle{new EnetHostHandle::Impl{rawHost}},
      &NetworkSystem::destroyEnetHost};
  mode_ = NetworkMode::Host;
  connected_ = true;
  return true;
#endif
}

bool NetworkSystem::createClient(const std::string &addressText,
                                 const std::uint16_t port,
                                 const std::uint8_t channels) {
#if !DEMI_HAS_ENET
  (void)addressText;
  (void)port;
  (void)channels;
  return false;
#else
  ENetAddress address{};
  if (enet_address_set_host(&address, addressText.c_str()) != 0) {
    return false;
  }
  address.port = port;
  auto *rawHost = enet_host_create(nullptr, 1,
                                   std::max<std::uint8_t>(channels, 1), 0, 0);
  if (rawHost == nullptr) {
    return false;
  }
  host_ = std::unique_ptr<EnetHostHandle, void(*)(EnetHostHandle*)>{
      new EnetHostHandle{new EnetHostHandle::Impl{rawHost}},
      &NetworkSystem::destroyEnetHost};
  ENetPeer *rawPeer =
      enet_host_connect(rawHost, &address, std::max<std::uint8_t>(channels, 1), 0);
  if (rawPeer == nullptr) {
    host_.reset();
    return false;
  }
  serverPeer_ = std::unique_ptr<EnetPeerHandle, void(*)(EnetPeerHandle*)>{
      new EnetPeerHandle{new EnetPeerHandle::Impl{rawPeer}},
      &NetworkSystem::destroyEnetPeer};
  enet_peer_timeout(rawPeer, 0, 1000, 3000);
  mode_ = NetworkMode::Client;
  return true;
#endif
}

void NetworkSystem::disconnect() {
#if DEMI_HAS_ENET
  if (host_ != nullptr) {
    if (mode_ == NetworkMode::Client && serverPeer_ != nullptr) {
      enet_peer_disconnect_now(serverPeer_->impl->peer, 0);
    }
    host_.reset();
  }
  serverPeer_.reset();
  peers_.clear();
  connected_ = false;
#else
  connected_ = false;
#endif
}

void NetworkSystem::disconnectPeer(const std::uint32_t peerIdValue) {
#if DEMI_HAS_ENET
  if (mode_ != NetworkMode::Host) {
    return;
  }
  const auto found = peers_.find(peerIdValue);
  if (found == peers_.end() || found->second == nullptr) {
    return;
  }
  enet_peer_disconnect_now(found->second->impl->peer, 0);
  peers_.erase(found);
#else
  (void)peerIdValue;
#endif
}

bool NetworkSystem::send(const std::string &message,
                         const bool reliable,
                         const std::uint8_t channel,
                         const std::uint32_t peerIdValue) {
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
  if (message.size() > MaxMessageSize) {
    std::cerr << "NetworkSystem::send rejected message of " << message.size()
              << " bytes (max " << MaxMessageSize << ").\n";
    return false;
  }

  if (dtls_->isEnabled()) {
    if (mode_ == NetworkMode::Client) {
      if (!dtls_->encrypt(0, message)) {
        return false;
      }
      for (const std::string &datagram : dtls_->takeDatagrams(0)) {
        if (!sendRaw(datagram, reliable, channel, 0, false)) {
          return false;
        }
      }
      return true;
    }
    if (mode_ == NetworkMode::Host && peerIdValue == 0) {
      bool sent = true;
      for (const auto &[peerId, _] : peers_) {
        sent = dtls_->encrypt(peerId, message) && sent;
        for (const std::string &datagram : dtls_->takeDatagrams(peerId)) {
          sent = sendRaw(datagram, reliable, channel, peerId, false) && sent;
        }
      }
      return sent;
    }
    if (!dtls_->encrypt(peerIdValue, message)) {
      return false;
    }
    for (const std::string &datagram : dtls_->takeDatagrams(peerIdValue)) {
      if (!sendRaw(datagram, reliable, channel, peerIdValue, false)) {
        return false;
      }
    }
    return true;
  }
  return sendRaw(message, reliable, channel, peerIdValue, true);
#endif
}

bool NetworkSystem::sendRaw(const std::string &message,
                            const bool reliable,
                            const std::uint8_t channel,
                            const std::uint32_t peerIdValue,
                            const bool frameMessage) {
#if !DEMI_HAS_ENET
  (void)message;
  (void)reliable;
  (void)channel;
  (void)peerIdValue;
  (void)frameMessage;
  return false;
#else
  const std::string &payload = frameMessage ? NetworkSystem::frameMessage(message) : message;
  if (mode_ == NetworkMode::Client) {
    if (serverPeer_ == nullptr) {
      return false;
    }
    ENetPacket *packet = makePacket(payload, reliable);
    if (packet == nullptr ||
        enet_peer_send(serverPeer_->impl->peer, channel, packet) != 0) {
      if (packet != nullptr) {
        enet_packet_destroy(packet);
      }
      return false;
    }
    enet_host_flush(host_->impl->host);
    return true;
  }

  if (mode_ == NetworkMode::Host) {
    ENetPacket *packet = makePacket(payload, reliable);
    if (packet == nullptr) {
      return false;
    }
    if (peerIdValue == 0) {
      enet_host_broadcast(host_->impl->host, channel, packet);
      enet_host_flush(host_->impl->host);
      return true;
    }
    const auto found = peers_.find(peerIdValue);
    if (found == peers_.end() || found->second == nullptr ||
        enet_peer_send(found->second->impl->peer, channel, packet) !=
            0) {
      enet_packet_destroy(packet);
      return false;
    }
    enet_host_flush(host_->impl->host);
    return true;
  }
  return false;
#endif
}

void NetworkSystem::sendDtlsDatagrams(const std::uint32_t peerIdValue) {
#if DEMI_HAS_ENET
  for (const std::string &datagram : dtls_->takeDatagrams(peerIdValue)) {
    (void)sendRaw(datagram, true, 0, peerIdValue, false);
  }
#else
  (void)peerIdValue;
#endif
}

void NetworkSystem::update(const std::uint32_t maxEvents) {
#if DEMI_HAS_ENET
  if (host_ == nullptr) {
    return;
  }

  auto *enetHost = host_->impl->host;
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
        peers_[id] = new EnetPeerHandle{new EnetPeerHandle::Impl{event.peer}};
      } else {
        id = 0;
        serverPeer_ = std::unique_ptr<EnetPeerHandle, void(*)(EnetPeerHandle*)>{
            new EnetPeerHandle{new EnetPeerHandle::Impl{event.peer}},
            &NetworkSystem::destroyEnetPeer};
        connected_ = !dtls_->isEnabled();
      }
      if (dtls_->isEnabled()) {
        if (!dtls_->addPeer(id)) {
          enet_peer_disconnect_now(event.peer, 0);
        }
        sendDtlsDatagrams(id);
      } else {
        events_.push_back(NetworkEvent{.type = NetworkEventType::Connected,
                                       .peerId = id,
                                       .channel = 0,
                                       .message = {},
                                       .latencyMs = event.peer != nullptr
                                                    ? event.peer->roundTripTime
                                                    : 0});
      }
    } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
      const std::uint32_t id =
          mode_ == NetworkMode::Host ? peerId(event.peer) : 0;
      const auto *bytes =
          static_cast<const char *>(static_cast<const void *>(event.packet->data));
      const std::size_t byteCount = event.packet->dataLength;
      if (dtls_->isEnabled()) {
        const std::string datagram(bytes, byteCount);
        (void)dtls_->receiveDatagram(id, datagram);
        sendDtlsDatagrams(id);
        for (std::string &plaintext : dtls_->takePlaintext(id)) {
          events_.push_back(NetworkEvent{.type = NetworkEventType::Message,
                                         .peerId = id,
                                         .channel = event.channelID,
                                         .message = std::move(plaintext),
                                         .latencyMs =
                                             event.peer != nullptr
                                                 ? event.peer->roundTripTime
                                                 : 0});
        }
      } else {
        if (const auto unframed = NetworkSystem::unframeMessage(bytes, byteCount);
            unframed.has_value()) {
          events_.push_back(NetworkEvent{.type = NetworkEventType::Message,
                                         .peerId = id,
                                         .channel = event.channelID,
                                         .message = *unframed,
                                         .latencyMs =
                                             event.peer != nullptr
                                                 ? event.peer->roundTripTime
                                                 : 0});
        } else {
          std::cerr << "NetworkSystem dropped malformed non-DTLS packet of "
                    << byteCount << " bytes from peer " << id << ".\n";
        }
      }
      enet_packet_destroy(event.packet);
    } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
      const std::uint32_t id = mode_ == NetworkMode::Host ? peerId(event.peer) : 0;
      if (mode_ == NetworkMode::Host) {
        peers_.erase(id);
      } else {
        connected_ = false;
        serverPeer_.reset();
      }
      dtls_->removePeer(id);
      if (event.peer != nullptr) {
        event.peer->data = nullptr;
      }
      events_.push_back(NetworkEvent{.type = NetworkEventType::Disconnected,
                                     .peerId = id,
                                     .channel = 0,
                                     .message = {},
                                     .latencyMs = 0});
    }
  }
  if (dtls_->isEnabled()) {
    dtls_->update();
    if (mode_ == NetworkMode::Client) {
      sendDtlsDatagrams(0);
    } else {
      for (const auto& [peerIdValue, _] : peers_) {
        sendDtlsDatagrams(peerIdValue);
      }
    }
    for (const std::uint32_t id : dtls_->takeReadyPeers()) {
      if (mode_ == NetworkMode::Client) {
        connected_ = true;
      }
      events_.push_back(NetworkEvent{
          .type = NetworkEventType::Connected,
          .peerId = id,
          .channel = 0,
          .message = {},
          .latencyMs = latencyMs()});
    }
    for (const std::uint32_t id : dtls_->takeFailedPeers()) {
      if (mode_ == NetworkMode::Client && serverPeer_ != nullptr) {
        enet_peer_disconnect_now(serverPeer_->impl->peer, 0);
        connected_ = false;
      } else {
        const auto found = peers_.find(id);
        if (found != peers_.end() && found->second != nullptr) {
          enet_peer_disconnect_now(found->second->impl->peer, 0);
          peers_.erase(found);
        }
      }
      dtls_->removePeer(id);
      events_.push_back(NetworkEvent{
          .type = NetworkEventType::Disconnected,
          .peerId = id,
          .channel = 0,
          .message = {},
          .latencyMs = 0});
    }
  }
#else
  (void)maxEvents;
#endif
}

std::vector<NetworkEvent> NetworkSystem::drainEvents() {
  std::vector<NetworkEvent> result = std::move(events_);
  events_.clear();
  return result;
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
    return serverPeer_->impl->peer->roundTripTime;
  }
  if (mode_ == NetworkMode::Host && !peers_.empty() &&
      peers_.begin()->second) {
    return peers_.begin()->second->impl->peer->roundTripTime;
  }
#endif
  return 0;
}

bool NetworkSystem::isSecure() const {
  return dtls_->isEnabled();
}

const std::string &NetworkSystem::securityError() const {
  return dtls_->lastError();
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