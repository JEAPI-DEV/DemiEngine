#pragma once

#include "demi/runtime/network/GameNetworkSession.h"
#include "demi/runtime/network/NetworkMessageGateway.h"
#include "demi/runtime/network/NetworkOwnershipRegistry.h"
#include "demi/runtime/network/NetworkPrediction.h"

#include <functional>

namespace demi::runtime {

// Session policy around the transport-independent prediction primitives.
// Gameplay supplies a JSON state transition; adapters translate callback errors
// to an empty result. This owner disables failed prediction and clears
// histories.
class NetworkSessionPrediction {
public:
  struct Config {
    NetworkOwnerInputQueue::Config inputQueue;
    NetworkSnapshotInterpolator::Config interpolation;
    NetworkPredictedController::Config controller;
    std::size_t maximumInputsPerTick = 0;
  };
  using Apply = std::function<std::optional<nlohmann::json>(
      const nlohmann::json &, const nlohmann::json &)>;

  NetworkSessionPrediction(const NetworkOwnershipRegistry &ownership,
                           const std::string &localPeer,
                           GameNetworkSession &diagnostics);
  void configure(Config config);
  void clear();
  void remove(const std::string &networkId);
  [[nodiscard]] bool enable(const NetworkContract *contract,
                            const std::string &networkId,
                            std::string inputMessage, nlohmann::json state,
                            Apply apply, double now);
  [[nodiscard]] bool disable(const std::string &networkId);
  [[nodiscard]] bool rebase(const std::string &networkId, nlohmann::json state);
  [[nodiscard]] std::optional<NetworkEnvelope>
  predict(const std::string &networkId, const nlohmann::json &input);
  [[nodiscard]] bool acceptInput(const NetworkContract &contract,
                                 const NetworkEnvelope &envelope,
                                 const std::string &trustedSender, double now);
  [[nodiscard]] nlohmann::json takeInputs(const std::string &networkId,
                                          double now);
  [[nodiscard]] std::optional<NetworkEnvelope>
  publish(const NetworkContract *contract, const std::string &networkId,
          nlohmann::json state, const std::string &marker);
  void acceptSnapshot(const std::string &networkId,
                      NetworkAuthoritySnapshot snapshot, double now);
  [[nodiscard]] nlohmann::json state(const std::string &networkId) const;
  [[nodiscard]] nlohmann::json visualOffset(const std::string &networkId,
                                            double now);
  [[nodiscard]] nlohmann::json remoteState(const std::string &networkId,
                                           double now);
  [[nodiscard]] nlohmann::json diagnostics() const;
  [[nodiscard]] std::vector<nlohmann::json> drainEvents();

private:
  struct Channel {
    explicit Channel(const Config &config);
    NetworkOwnerInputQueue serverQueue;
    NetworkSnapshotInterpolator interpolator;
    NetworkPredictedController controller;
    std::string inputMessage;
    Apply apply;
    std::uint64_t serverTick = 0;
    double lastVisualUpdateSeconds = 0.0;
  };
  Channel &channel(const std::string &networkId);
  bool apply(Channel &channel, const nlohmann::json &input);
  void queueEvent(const std::string &name, const std::string &networkId,
                  nlohmann::json data);

  const NetworkOwnershipRegistry &ownership_;
  const std::string &localPeer_;
  GameNetworkSession &diagnostics_;
  Config config_;
  std::map<std::string, Channel> channels_;
  std::vector<nlohmann::json> events_;
};

} // namespace demi::runtime
