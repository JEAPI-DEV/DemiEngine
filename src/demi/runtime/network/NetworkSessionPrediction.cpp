#include "demi/runtime/network/NetworkSessionPrediction.h"

#include <algorithm>

namespace demi::runtime {
namespace {
bool isInputRule(const NetworkMessageRule &rule) {
  return rule.from == NetworkActor::Owner && rule.target == "owned_entity" &&
         (rule.to == NetworkActor::Server || rule.to == NetworkActor::All);
}
} // namespace

NetworkSessionPrediction::Channel::Channel(const Config &config)
    : serverQueue(config.inputQueue), interpolator(config.interpolation),
      controller(config.controller) {}

NetworkSessionPrediction::NetworkSessionPrediction(
    const NetworkOwnershipRegistry &ownership, const std::string &localPeer,
    GameNetworkSession &diagnostics)
    : ownership_(ownership), localPeer_(localPeer), diagnostics_(diagnostics) {}

void NetworkSessionPrediction::configure(Config config) {
  config_ = std::move(config);
}
void NetworkSessionPrediction::clear() {
  channels_.clear();
  events_.clear();
}
void NetworkSessionPrediction::remove(const std::string &networkId) {
  channels_.erase(networkId);
  std::erase_if(events_, [&](const nlohmann::json &event) {
    return event.at("target") == networkId;
  });
}
NetworkSessionPrediction::Channel &
NetworkSessionPrediction::channel(const std::string &networkId) {
  return channels_.try_emplace(networkId, config_).first->second;
}

bool NetworkSessionPrediction::enable(const NetworkContract *contract,
                                      const std::string &networkId,
                                      std::string inputMessage,
                                      nlohmann::json state, Apply applyCallback,
                                      double now) {
  if (contract == nullptr || ownership_.authoritativeServer() ||
      localPeer_.empty() || !ownership_.isOwner(networkId, localPeer_)) {
    diagnostics_.reject(
        "prediction requires a contract entity owned by the local client");
    return false;
  }
  const auto rule = contract->messages.find(inputMessage);
  if (rule == contract->messages.end() || !isInputRule(rule->second)) {
    diagnostics_.reject(
        "prediction requires a declared owner-to-server owned_entity intent");
    return false;
  }
  if (config_.controller.inputHistoryLimit == 0 || !state.is_object() ||
      !applyCallback) {
    diagnostics_.reject("prediction requires bounded history, an object state "
                        "and an apply callback");
    return false;
  }
  auto &entry = channel(networkId);
  entry.inputMessage = std::move(inputMessage);
  entry.apply = std::move(applyCallback);
  entry.lastVisualUpdateSeconds = now;
  entry.controller.enable(ownership_.sessionEpoch(),
                          ownership_.find(networkId)->ownershipGeneration,
                          std::move(state));
  return true;
}

bool NetworkSessionPrediction::disable(const std::string &networkId) {
  const auto found = channels_.find(networkId);
  if (found == channels_.end() || !found->second.controller.enabled())
    return false;
  found->second.controller.disable();
  found->second.apply = {};
  return true;
}

bool NetworkSessionPrediction::rebase(const std::string &networkId,
                                      nlohmann::json state) {
  const auto found = channels_.find(networkId);
  if (found == channels_.end() || !found->second.controller.enabled() ||
      !state.is_object())
    return false;
  found->second.controller.rebaseState(std::move(state));
  return true;
}

bool NetworkSessionPrediction::apply(Channel &entry,
                                     const nlohmann::json &input) {
  const auto next =
      entry.apply ? entry.apply(entry.controller.state(), input) : std::nullopt;
  if (!next || !next->is_object()) {
    diagnostics_.reject("prediction callback failed or returned a non-object "
                        "state; prediction disabled");
    entry.controller.disable();
    entry.apply = {};
    return false;
  }
  entry.controller.setPredictedState(*next);
  return true;
}

std::optional<NetworkEnvelope>
NetworkSessionPrediction::predict(const std::string &networkId,
                                  const nlohmann::json &input) {
  const auto found = channels_.find(networkId);
  if (found == channels_.end() || !found->second.controller.enabled()) {
    diagnostics_.reject("prediction is not enabled for " + networkId);
    return std::nullopt;
  }
  if (!ownership_.isOwner(networkId, localPeer_)) {
    (void)disable(networkId);
    diagnostics_.reject("prediction stopped because local ownership was lost");
    return std::nullopt;
  }
  if (!input.is_object()) {
    diagnostics_.reject("predicted input must be an object");
    return std::nullopt;
  }
  auto &entry = found->second;
  const auto sequence = entry.controller.recordLocalInput(input);
  if (sequence == 0 || !apply(entry, input))
    return std::nullopt;
  auto payload = input;
  payload["seq"] = sequence;
  return NetworkEnvelope{.kind = NetworkEnvelopeKind::Message,
                         .ownershipGeneration =
                             ownership_.find(networkId)->ownershipGeneration,
                         .name = entry.inputMessage,
                         .target = networkId,
                         .data = std::move(payload)};
}

bool NetworkSessionPrediction::acceptInput(const NetworkContract &contract,
                                           const NetworkEnvelope &envelope,
                                           const std::string &trustedSender,
                                           double now) {
  const auto rule = contract.messages.find(envelope.name);
  if (!ownership_.authoritativeServer() || config_.inputQueue.capacity == 0 ||
      rule == contract.messages.end() || !isInputRule(rule->second) ||
      ownership_.find(envelope.target) == nullptr ||
      !envelope.data.is_object() || !envelope.data.contains("seq"))
    return false;
  const auto *entity = ownership_.find(envelope.target);
  if (entity->ownerPeerId != trustedSender ||
      envelope.sessionEpoch != ownership_.sessionEpoch() ||
      envelope.ownershipGeneration != entity->ownershipGeneration) {
    diagnostics_.reject("predicted input does not match current ownership");
    return true;
  }
  if (!envelope.data["seq"].is_number_unsigned()) {
    diagnostics_.reject("predicted input sequence must be unsigned");
    return true;
  }
  auto &entry = channel(envelope.target);
  if (!entry.inputMessage.empty() && entry.inputMessage != envelope.name)
    return false;
  entry.inputMessage = envelope.name;
  auto input = envelope.data;
  const auto sequence = input["seq"].get<std::uint64_t>();
  input.erase("seq");
  const auto accepted =
      entry.serverQueue.submit(sequence, now, std::move(input));
  if (accepted != NetworkInputRejectCode::None)
    diagnostics_.reject("predicted input rejected: " +
                        std::string(networkInputRejectCodeName(accepted)));
  return true;
}

nlohmann::json
NetworkSessionPrediction::takeInputs(const std::string &networkId, double now) {
  auto result = nlohmann::json::array();
  if (!ownership_.authoritativeServer() ||
      ownership_.find(networkId) == nullptr) {
    diagnostics_.reject(
        "only the server may take inputs for a declared entity");
    return result;
  }
  for (const auto &command : channel(networkId).serverQueue.evaluate(
           now, config_.maximumInputsPerTick)) {
    if (!command.discarded && command.payload.is_object()) {
      auto input = command.payload;
      input["seq"] = command.sequence;
      result.push_back(std::move(input));
    }
  }
  return result;
}

std::optional<NetworkEnvelope> NetworkSessionPrediction::publish(
    const NetworkContract *contract, const std::string &networkId,
    nlohmann::json state, const std::string &marker) {
  const auto *entity = ownership_.find(networkId);
  if (contract == nullptr || !ownership_.authoritativeServer() ||
      entity == nullptr || !state.is_object()) {
    diagnostics_.reject(
        "only the server may publish object snapshots for a declared entity");
    return std::nullopt;
  }
  if (marker != "normal" && marker != "teleport" && marker != "reset") {
    diagnostics_.reject("unknown snapshot marker: " + marker);
    return std::nullopt;
  }
  auto &entry = channel(networkId);
  nlohmann::json data = {{"tick", entry.serverTick + 1},
                         {"ack", entry.serverQueue.acknowledgedSequence()},
                         {"marker", marker},
                         {"state", std::move(state)}};
  if (const auto &rejection = entry.serverQueue.lastRejection()) {
    data["rejected_sequence"] = rejection->first;
    data["rejection"] = rejection->second;
  }
  if (data.dump().size() > contract->limits.maximumMessageBytes) {
    diagnostics_.reject(
        "snapshot state exceeds the declared message byte limit");
    return std::nullopt;
  }
  ++entry.serverTick;
  entry.serverQueue.clearRejection();
  return NetworkEnvelope{.kind = NetworkEnvelopeKind::Snapshot,
                         .ownershipGeneration = entity->ownershipGeneration,
                         .name = "snapshot",
                         .target = networkId,
                         .data = std::move(data)};
}

void NetworkSessionPrediction::queueEvent(const std::string &name,
                                          const std::string &networkId,
                                          nlohmann::json data) {
  events_.push_back({{"name", name},
                     {"sender_id", "server"},
                     {"target", networkId},
                     {"data", std::move(data)}});
}

void NetworkSessionPrediction::acceptSnapshot(const std::string &networkId,
                                              NetworkAuthoritySnapshot snapshot,
                                              double now) {
  const auto *entity = ownership_.find(networkId);
  if (entity == nullptr || snapshot.sessionEpoch != ownership_.sessionEpoch() ||
      snapshot.ownershipGeneration != entity->ownershipGeneration)
    return;
  auto &entry = channel(networkId);
  snapshot.receivedAtSeconds = now;
  if (!ownership_.isOwner(networkId, localPeer_) ||
      !entry.controller.enabled()) {
    (void)entry.interpolator.push(snapshot);
    return;
  }
  const auto reconciliation = entry.controller.reconcile(snapshot);
  if (!reconciliation.applied)
    return;
  for (const auto &command : reconciliation.replay)
    if (!apply(entry, command.payload))
      return;
  if (entry.controller.commitReplay()) {
    entry.lastVisualUpdateSeconds = now;
    queueEvent(
        "prediction_corrected", networkId,
        {{"distance", entry.controller.counters().lastCorrectionDistance},
         {"replayed", reconciliation.replay.size()},
         {"acknowledged", reconciliation.acknowledgedSequence}});
  }
  if (reconciliation.snapped)
    queueEvent("prediction_snapped", networkId,
               {{"rebased", reconciliation.rebased}});
}

nlohmann::json
NetworkSessionPrediction::state(const std::string &networkId) const {
  const auto found = channels_.find(networkId);
  if (found == channels_.end() || !found->second.controller.enabled())
    return nullptr;
  return found->second.controller.state();
}
nlohmann::json
NetworkSessionPrediction::visualOffset(const std::string &networkId,
                                       double now) {
  const auto found = channels_.find(networkId);
  if (found == channels_.end() || !found->second.controller.enabled())
    return nullptr;
  auto &entry = found->second;
  entry.controller.updateVisualOffset(
      std::max(now - entry.lastVisualUpdateSeconds, 0.0));
  entry.lastVisualUpdateSeconds = now;
  if (entry.controller.visualOffset().empty())
    return nullptr;
  return entry.controller.visualOffset();
}
nlohmann::json
NetworkSessionPrediction::remoteState(const std::string &networkId,
                                      double now) {
  const auto found = channels_.find(networkId);
  if (found == channels_.end())
    return nullptr;
  const auto sample = found->second.interpolator.sample(now);
  return sample ? sample->state : nlohmann::json(nullptr);
}
std::vector<nlohmann::json> NetworkSessionPrediction::drainEvents() {
  auto events = std::move(events_);
  events_.clear();
  return events;
}

nlohmann::json NetworkSessionPrediction::diagnostics() const {
  nlohmann::json result = nlohmann::json::object();
  nlohmann::json channels = nlohmann::json::object();
  for (const auto &[networkId, channel] : channels_) {
    nlohmann::json entry = nlohmann::json::object();
    entry["prediction_enabled"] = channel.controller.enabled();
    entry["input_message"] = channel.inputMessage;
    entry["next_sequence"] = channel.controller.nextSequence();
    entry["pending_replay"] = channel.controller.pendingReplayCount();
    const NetworkPredictionCounters &counters = channel.controller.counters();
    entry["corrections"] = counters.corrections;
    entry["replayed_commands"] = counters.replayedCommands;
    entry["discarded_inputs"] = counters.discardedInputs;
    entry["dropped_history"] = counters.droppedHistory;
    entry["snaps"] = counters.snaps;
    entry["rebases"] = counters.rebases;
    entry["ownership_changes"] = counters.ownershipChanges;
    entry["stale_snapshots"] = counters.staleSnapshots;
    entry["last_correction_distance"] = counters.lastCorrectionDistance;
    entry["last_divergence"] = counters.lastDivergence;
    nlohmann::json offset = nlohmann::json::object();
    for (const auto &[axis, value] : channel.controller.visualOffset())
      offset[axis] = value;
    entry["visual_offset"] = offset;
    nlohmann::json server = nlohmann::json::object();
    server["last_acked"] = channel.serverQueue.acknowledgedSequence();
    server["pending"] = channel.serverQueue.pending();
    const NetworkInputQueueCounters &queue = channel.serverQueue.counters();
    server["accepted"] = queue.accepted;
    server["rejected_old"] = queue.rejectedOld;
    server["rejected_duplicate"] = queue.rejectedDuplicate;
    server["rejected_future"] = queue.rejectedFuture;
    server["rejected_capacity"] = queue.rejectedCapacity;
    server["rejected_malformed"] = queue.rejectedMalformed;
    server["discarded_gaps"] = queue.discardedGaps;
    server["discarded_rejected"] = queue.discardedRejected;
    entry["server"] = server;
    nlohmann::json interpolation = nlohmann::json::object();
    interpolation["buffer_depth"] = channel.interpolator.depth();
    const NetworkSnapshotBufferCounters &buffer =
        channel.interpolator.counters();
    interpolation["accepted"] = buffer.accepted;
    interpolation["dropped_stale"] = buffer.droppedStale;
    interpolation["dropped_overflow"] = buffer.droppedOverflow;
    interpolation["cleared_for_epoch"] = buffer.clearedForEpoch;
    interpolation["cleared_for_generation"] = buffer.clearedForGeneration;
    interpolation["interpolated"] = buffer.interpolated;
    interpolation["extrapolated"] = buffer.extrapolated;
    interpolation["clamped"] = buffer.clamped;
    interpolation["snapped"] = buffer.snapped;
    entry["interpolation"] = interpolation;
    channels[networkId] = entry;
  }
  result["channels"] = channels;
  return result;
}

} // namespace demi::runtime
