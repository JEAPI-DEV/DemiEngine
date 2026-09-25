#pragma once

#include "demi/runtime/network/GameNetworkSession.h"
#include "demi/runtime/network/NetworkMessageGateway.h"
#include "demi/runtime/network/NetworkOwnershipRegistry.h"
#include "demi/runtime/network/NetworkPrediction.h"
#include "demi/runtime/network/NetworkSessionLifecycle.h"
#include "demi/runtime/network/NetworkSessionPrediction.h"

#include <functional>
#include <map>
#include <utility>

namespace demi::runtime {

enum class NetworkPublicationStatus { Rejected, Waiting, Due };

// A received position is sampled against the game clock, never advanced by
// calls made for other entities. Interpolated prediction samples are final.
struct NetworkRemoteMotion2D {
  float x = 0.0F;
  float y = 0.0F;
  float vx = 0.0F;
  float vy = 0.0F;
  double receivedAtSeconds = 0.0;
  bool alreadySampled = false;

  [[nodiscard]] std::pair<float, float>
  position(double nowSeconds, double extrapolationLimit,
           double initialPrediction = 0.0) const;
};

// Coordinates the contract gateway, ownership and retained replication state.
// Transport and scene adapters supply bytes/state; neither Lua nor a world is
// required to enforce the session protocol.
class NetworkSessionProtocol {
public:
  using Send = std::function<bool(const std::string &, bool, std::uint32_t)>;

  explicit NetworkSessionProtocol(GameNetworkSession &diagnostics);
  NetworkSessionProtocol(const NetworkSessionProtocol &) = delete;
  NetworkSessionProtocol &operator=(const NetworkSessionProtocol &) = delete;
  NetworkSessionProtocol(NetworkSessionProtocol &&) = delete;
  NetworkSessionProtocol &operator=(NetworkSessionProtocol &&) = delete;
  [[nodiscard]] static std::optional<NetworkAuthoritySnapshot>
  parseSnapshot(const NetworkEnvelope &envelope);
  void reset(bool hosting);
  void activateHost();
  void connected();

  [[nodiscard]] const NetworkOwnershipRegistry &ownership() const;
  [[nodiscard]] NetworkSessionPrediction &prediction();
  [[nodiscard]] const NetworkGatewayCounters &counters() const;
  [[nodiscard]] NetworkSessionPhase phase() const;
  [[nodiscard]] bool ready() const;
  [[nodiscard]] const std::string &localPeerId() const;
  [[nodiscard]] NetworkPublicationStatus
  publicationStatus(const std::string &networkId, double deltaSeconds,
                    double sendInterval);

  [[nodiscard]] bool send(const NetworkContract *contract,
                          NetworkEnvelope envelope, const Send &transport,
                          std::uint32_t peerId = 0);
  [[nodiscard]] std::optional<NetworkEnvelope>
  message(const NetworkContract *contract, std::string name, std::string target,
          nlohmann::json data);
  [[nodiscard]] std::optional<NetworkEnvelope>
  relayMessage(const NetworkContract &contract, const NetworkEnvelope &envelope,
               const std::string &trustedSender) const;
  [[nodiscard]] nlohmann::json
  gameEvent(const NetworkEnvelope &envelope,
            const std::string &trustedSender) const;
  [[nodiscard]] NetworkGatewayResult receive(const NetworkContract *contract,
                                             std::string_view wire,
                                             std::string trustedSender,
                                             double nowSeconds);

  [[nodiscard]] std::optional<NetworkEnvelope>
  spawn(const NetworkContract *contract, const std::string &prefabKey,
        const std::string &entityId, std::string owner, nlohmann::json state);
  [[nodiscard]] std::optional<NetworkEnvelope>
  transfer(const NetworkContract *contract, const std::string &networkId,
           const std::string &owner);
  [[nodiscard]] std::optional<NetworkEnvelope>
  despawn(const NetworkContract *contract, const std::string &networkId);
  [[nodiscard]] std::vector<NetworkEnvelope>
  disconnectPeer(const NetworkContract &contract, std::string_view peer);
  [[nodiscard]] std::vector<NetworkEnvelope>
  lateJoin(const NetworkContract &contract, const std::string &peer,
           const nlohmann::json &metadata) const;

private:
  void activate();
  void retainOwner(const NetworkOwnedEntity &entity);
  void invalidateEntity(const std::string &networkId);
  [[nodiscard]] bool checkOwnership(const OwnershipResult &result);
  [[nodiscard]] NetworkGatewayResult reject(NetworkGatewayResult result,
                                            std::string reason);

  GameNetworkSession &diagnostics_;
  NetworkOwnershipRegistry ownership_;
  NetworkMessageGateway gateway_;
  NetworkSessionLifecycle lifecycle_;
  std::uint64_t outgoingSequence_ = 1;
  bool ready_ = false;
  std::string localPeerId_;
  NetworkSessionPrediction prediction_{ownership_, localPeerId_, diagnostics_};
  std::map<std::string, nlohmann::json> retainedSpawns_;
  std::map<std::string, double> publicationElapsed_;
};

} // namespace demi::runtime
