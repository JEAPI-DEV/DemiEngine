#include "demi/runtime/network/GameNetworkSession.h"

#include <utility>

namespace demi::runtime {

void GameNetworkSession::reset(const bool hosting) {
  hosting_ = hosting;
  localPeerId_ = hosting ? "host" : "client";
  peers_.clear();
  owners_.clear();
  diagnostics_ = {};
  latencyMs_ = 0;
}

void GameNetworkSession::setLocalPeerId(std::string peerId) {
  if (!peerId.empty())
    localPeerId_ = std::move(peerId);
}

std::string_view GameNetworkSession::localPeerId() const {
  return localPeerId_;
}

void GameNetworkSession::peerConnected(const std::uint32_t peerId) {
  peers_[peerId] = peerName(peerId);
  diagnostics_.connectedPeers = static_cast<std::uint32_t>(peers_.size());
}

void GameNetworkSession::peerDisconnected(const std::uint32_t peerId) {
  const std::string disconnected = peerName(peerId);
  peers_.erase(peerId);
  for (auto iterator = owners_.begin(); iterator != owners_.end();) {
    if (iterator->second == disconnected)
      iterator = owners_.erase(iterator);
    else
      ++iterator;
  }
  diagnostics_.connectedPeers = static_cast<std::uint32_t>(peers_.size());
}

std::string GameNetworkSession::peerName(const std::uint32_t peerId) const {
  return "peer" + std::to_string(peerId);
}

bool GameNetworkSession::registerEntity(std::string networkId,
                                        std::string ownerId) {
  if (networkId.empty() || ownerId.empty()) {
    reject("network entity id and owner are required");
    return false;
  }
  if (const auto found = owners_.find(networkId); found != owners_.end()) {
    if (found->second == ownerId)
      return true;
    reject("network entity is already owned by another peer");
    return false;
  }
  owners_.emplace(std::move(networkId), std::move(ownerId));
  return true;
}

bool GameNetworkSession::removeEntity(const std::string_view networkId) {
  return owners_.erase(std::string(networkId)) != 0;
}

bool GameNetworkSession::setOwner(const std::string_view networkId,
                                  std::string ownerId) {
  const auto found = owners_.find(std::string(networkId));
  if (found == owners_.end() || ownerId.empty()) {
    reject("cannot assign authority for unknown network entity");
    return false;
  }
  found->second = std::move(ownerId);
  return true;
}

std::optional<std::string>
GameNetworkSession::owner(const std::string_view networkId) const {
  const auto found = owners_.find(std::string(networkId));
  return found == owners_.end() ? std::nullopt
                               : std::optional<std::string>(found->second);
}

bool GameNetworkSession::hasAuthority(const std::string_view networkId) const {
  const auto entityOwner = owner(networkId);
  return entityOwner.has_value() &&
         (entityOwner == localPeerId_ || (hosting_ && *entityOwner == "host"));
}

bool GameNetworkSession::acceptsStateFrom(const std::string_view networkId,
                                          const std::string_view sender) const {
  const auto entityOwner = owner(networkId);
  return entityOwner.has_value() && *entityOwner == sender;
}

void GameNetworkSession::messageSent() {
  ++diagnostics_.sentMessages;
}

void GameNetworkSession::messageReceived() {
  ++diagnostics_.receivedMessages;
}

void GameNetworkSession::reject(std::string error) {
  ++diagnostics_.rejectedMessages;
  diagnostics_.lastError = std::move(error);
}

void GameNetworkSession::setLatency(const std::uint32_t latencyMs) {
  latencyMs_ = latencyMs;
}

std::uint32_t GameNetworkSession::latencyMs() const {
  return latencyMs_;
}

const NetworkDiagnostics &GameNetworkSession::diagnostics() const {
  return diagnostics_;
}

} // namespace demi::runtime
