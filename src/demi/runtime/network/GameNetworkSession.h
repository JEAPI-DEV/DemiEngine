#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace demi::runtime {

struct NetworkDiagnostics {
  std::uint64_t sentMessages = 0;
  std::uint64_t receivedMessages = 0;
  std::uint64_t rejectedMessages = 0;
  std::uint32_t connectedPeers = 0;
  std::string lastError;
};

// Transport-independent game session rules. This class owns identity and
// authority decisions; NetworkSystem only moves its serialized messages.
class GameNetworkSession {
public:
  void reset(bool hosting);
  void setLocalPeerId(std::string peerId);
  [[nodiscard]] std::string_view localPeerId() const;

  void peerConnected(std::uint32_t peerId);
  void peerDisconnected(std::uint32_t peerId);
  [[nodiscard]] std::string peerName(std::uint32_t peerId) const;

  [[nodiscard]] bool registerEntity(std::string networkId, std::string owner);
  [[nodiscard]] bool removeEntity(std::string_view networkId);
  [[nodiscard]] bool setOwner(std::string_view networkId, std::string owner);
  [[nodiscard]] std::optional<std::string>
  owner(std::string_view networkId) const;
  [[nodiscard]] bool hasAuthority(std::string_view networkId) const;
  [[nodiscard]] bool acceptsStateFrom(std::string_view networkId,
                                      std::string_view sender) const;

  void messageSent();
  void messageReceived();
  void reject(std::string error);
  void setLatency(std::uint32_t latencyMs);
  [[nodiscard]] std::uint32_t latencyMs() const;
  [[nodiscard]] const NetworkDiagnostics &diagnostics() const;

private:
  bool hosting_ = false;
  std::string localPeerId_ = "client";
  std::unordered_map<std::uint32_t, std::string> peers_;
  std::unordered_map<std::string, std::string> owners_;
  NetworkDiagnostics diagnostics_;
  std::uint32_t latencyMs_ = 0;
};

} // namespace demi::runtime
