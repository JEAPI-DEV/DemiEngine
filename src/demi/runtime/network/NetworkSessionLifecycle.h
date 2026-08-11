#pragma once

#include "demi/runtime/network/NetworkOwnershipRegistry.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {

enum class NetworkSessionPhase {
  Closed,
  Connected,
  Authenticated,
  Ready,
  Active,
  Reconnecting,
};

class NetworkSessionLifecycle {
public:
  [[nodiscard]] bool transition(NetworkSessionPhase next);
  void reset();
  [[nodiscard]] NetworkSessionPhase phase() const;

private:
  NetworkSessionPhase phase_ = NetworkSessionPhase::Closed;
};

struct ReconnectEntityLease {
  std::string networkId;
  std::uint64_t ownershipGeneration = 0;
};

struct ReconnectLease {
  std::string token;
  std::string peerId;
  std::uint64_t sessionEpoch = 0;
  std::uint64_t expiresAtMillis = 0;
  std::vector<ReconnectEntityLease> entities;
};

enum class ReconnectRejectCode {
  None,
  UnknownToken,
  Expired,
  StaleEpoch,
  StaleOwnership,
};

struct ReconnectResult {
  bool accepted = false;
  ReconnectRejectCode code = ReconnectRejectCode::None;
  std::string peerId;
};

class ReconnectLeaseStore {
public:
  [[nodiscard]] std::string issue(std::string peerId,
                                  const NetworkOwnershipRegistry &ownership,
                                  std::uint64_t nowMillis,
                                  std::uint64_t lifetimeMillis);
  [[nodiscard]] ReconnectResult
  consume(std::string_view token, const NetworkOwnershipRegistry &ownership,
          std::uint64_t nowMillis);
  void revokePeer(std::string_view peerId);
  void reset();
  [[nodiscard]] std::size_t size() const;

private:
  [[nodiscard]] static std::string randomToken();
  std::map<std::string, ReconnectLease, std::less<>> leases_;
};

[[nodiscard]] std::string_view networkSessionPhaseName(NetworkSessionPhase phase);
[[nodiscard]] std::string_view reconnectRejectCodeName(ReconnectRejectCode code);

} // namespace demi::runtime
