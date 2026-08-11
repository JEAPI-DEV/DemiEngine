#pragma once

#include "demi/runtime/network/NetworkContract.h"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace demi::runtime {

class NetworkOwnershipRegistry;

enum class NetworkEnvelopeKind : std::uint8_t {
  Message = 1,
  Spawn = 2,
  Despawn = 3,
  Ownership = 4,
  Session = 5,
};

enum class NetworkGatewayRejectCode {
  None,
  Truncated,
  BadMagic,
  UnsupportedProtocol,
  ContractMismatch,
  StaleEpoch,
  StaleGeneration,
  Replay,
  Oversized,
  InvalidJson,
  ExcessiveDepth,
  ExcessiveElements,
  StringTooLong,
  NonFiniteNumber,
  SchemaViolation,
  UnknownMessage,
  UnauthorizedSender,
  UnauthorizedTarget,
  RateLimited,
  InvalidOperation,
};

struct NetworkGatewayCounters {
  std::uint64_t accepted = 0;
  std::map<NetworkGatewayRejectCode, std::uint64_t> rejected;
};

struct NetworkEnvelope {
  NetworkEnvelopeKind kind = NetworkEnvelopeKind::Message;
  std::uint64_t sessionEpoch = 0;
  std::uint64_t ownershipGeneration = 0;
  std::uint64_t sequence = 0;
  std::string name;
  std::string target;
  nlohmann::json data;
};

struct NetworkGatewayContext {
  bool authoritativeServer = false;
  std::string trustedSenderPeerId;
  std::string localPeerId;
  double nowSeconds = 0.0;
  const NetworkContract *contract = nullptr;
  const NetworkOwnershipRegistry *ownership = nullptr;
};

struct NetworkGatewayResult {
  bool accepted = false;
  NetworkGatewayRejectCode code = NetworkGatewayRejectCode::None;
  std::string internalReason;
  std::optional<NetworkEnvelope> envelope;
};

class NetworkMessageGateway {
public:
  static constexpr std::uint16_t ProtocolVersion = 1;
  static constexpr std::size_t HeaderBytes = 44;

  [[nodiscard]] std::vector<std::uint8_t>
  encode(const NetworkContract &contract, const NetworkEnvelope &envelope) const;
  [[nodiscard]] NetworkGatewayResult
  accept(std::span<const std::uint8_t> bytes,
         const NetworkGatewayContext &context);
  void reset();
  [[nodiscard]] const NetworkGatewayCounters &counters() const;

private:
  [[nodiscard]] NetworkGatewayResult reject(NetworkGatewayRejectCode code,
                                             std::string reason);

  struct RateWindow {
    std::deque<double> acceptedAt;
  };
  std::map<std::string, std::uint64_t> lastSequences_;
  std::map<std::string, RateWindow> rateWindows_;
  NetworkGatewayCounters counters_;
};

[[nodiscard]] std::string_view
networkGatewayRejectCodeName(NetworkGatewayRejectCode code);

} // namespace demi::runtime
