#include "demi/runtime/network/NetworkSessionProtocol.h"
#include "demi/runtime/network/ReplicatedState.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime {

std::pair<float, float>
NetworkRemoteMotion2D::position(double nowSeconds, double extrapolationLimit,
                                double initialPrediction) const {
  if (alreadySampled || !std::isfinite(nowSeconds) ||
      !std::isfinite(receivedAtSeconds) || !std::isfinite(extrapolationLimit) ||
      !std::isfinite(initialPrediction))
    return {x, y};
  const double age = std::max(nowSeconds - receivedAtSeconds, 0.0);
  const double offset = std::clamp(age + std::max(initialPrediction, 0.0), 0.0,
                                   std::max(extrapolationLimit, 0.0));
  return {x + vx * static_cast<float>(offset),
          y + vy * static_cast<float>(offset)};
}

namespace {

NetworkEnvelope spawnEnvelope(const nlohmann::json &spawn) {
  return {.kind = NetworkEnvelopeKind::Spawn,
          .ownershipGeneration = spawn.at("ownership_generation"),
          .name = spawn.at("prefab_key"),
          .target = spawn.at("network_id"),
          .data = spawn};
}

NetworkEnvelope ownerEnvelope(const NetworkOwnedEntity &entity) {
  return {.kind = NetworkEnvelopeKind::Ownership,
          .ownershipGeneration = entity.ownershipGeneration,
          .name = "ownership",
          .target = entity.networkId,
          .data = {{"owner", entity.ownerPeerId}}};
}

NetworkEnvelope despawnEnvelope(const NetworkOwnedEntity &entity) {
  return {.kind = NetworkEnvelopeKind::Despawn,
          .ownershipGeneration = entity.ownershipGeneration,
          .name = "despawn",
          .target = entity.networkId,
          .data = nlohmann::json::object()};
}

} // namespace

NetworkSessionProtocol::NetworkSessionProtocol(GameNetworkSession &diagnostics)
    : diagnostics_(diagnostics) {}

void NetworkSessionProtocol::reset(bool hosting) {
  publicationElapsed_.clear();
  prediction_.clear();
  ownership_.reset(hosting);
  gateway_.reset();
  lifecycle_.reset();
  outgoingSequence_ = 1;
  ready_ = false;
  localPeerId_.clear();
  retainedSpawns_.clear();
}

void NetworkSessionProtocol::activate() {
  (void)lifecycle_.transition(NetworkSessionPhase::Authenticated);
  (void)lifecycle_.transition(NetworkSessionPhase::Ready);
  (void)lifecycle_.transition(NetworkSessionPhase::Active);
  ready_ = true;
}

void NetworkSessionProtocol::activateHost() {
  reset(true);
  localPeerId_ = "server";
  connected();
  activate();
}

void NetworkSessionProtocol::connected() {
  (void)lifecycle_.transition(NetworkSessionPhase::Connected);
}

const NetworkOwnershipRegistry &NetworkSessionProtocol::ownership() const {
  return ownership_;
}
NetworkSessionPrediction &NetworkSessionProtocol::prediction() {
  return prediction_;
}
const NetworkGatewayCounters &NetworkSessionProtocol::counters() const {
  return gateway_.counters();
}
NetworkSessionPhase NetworkSessionProtocol::phase() const {
  return lifecycle_.phase();
}
bool NetworkSessionProtocol::ready() const { return ready_; }
const std::string &NetworkSessionProtocol::localPeerId() const {
  return localPeerId_;
}

NetworkPublicationStatus NetworkSessionProtocol::publicationStatus(
    const std::string &networkId, double deltaSeconds, double sendInterval) {
  if (!ownership_.isOwner(networkId, localPeerId_) ||
      !std::isfinite(deltaSeconds) || deltaSeconds < 0.0 ||
      !std::isfinite(sendInterval) || sendInterval < 0.0) {
    diagnostics_.reject(
        "publication requires local ownership and finite nonnegative timing");
    return NetworkPublicationStatus::Rejected;
  }
  double &elapsed = publicationElapsed_[networkId];
  const double advanced = elapsed + deltaSeconds;
  if (!std::isfinite(advanced)) {
    diagnostics_.reject("publication elapsed time overflowed");
    return NetworkPublicationStatus::Rejected;
  }
  elapsed = advanced;
  if (sendInterval > 0.0 && elapsed < sendInterval)
    return NetworkPublicationStatus::Waiting;
  elapsed = sendInterval > 0.0 ? std::fmod(elapsed, sendInterval) : 0.0;
  return NetworkPublicationStatus::Due;
}

void NetworkSessionProtocol::invalidateEntity(const std::string &networkId) {
  publicationElapsed_.erase(networkId);
  prediction_.remove(networkId);
}

bool NetworkSessionProtocol::send(const NetworkContract *contract,
                                  NetworkEnvelope envelope,
                                  const Send &transport, std::uint32_t peerId) {
  if (contract == nullptr) {
    diagnostics_.reject("project does not declare a network_contract asset");
    return false;
  }
  envelope.sessionEpoch = ownership_.sessionEpoch();
  envelope.sequence = outgoingSequence_++;
  const auto bytes = gateway_.encode(*contract, envelope);
  const bool reliable = envelope.kind != NetworkEnvelopeKind::Message ||
                        (contract->messages.contains(envelope.name) &&
                         contract->messages.at(envelope.name).reliability ==
                             NetworkReliability::Reliable);
  const std::string wire(reinterpret_cast<const char *>(bytes.data()),
                         bytes.size());
  if (!transport(wire, reliable, peerId)) {
    diagnostics_.reject("failed to send declared network operation");
    return false;
  }
  diagnostics_.messageSent();
  return true;
}

bool NetworkSessionProtocol::checkOwnership(const OwnershipResult &result) {
  if (!result.accepted) {
    diagnostics_.reject(std::string(ownershipRejectCodeName(result.code)) +
                        ": " + result.reason);
  }
  return result.accepted;
}

std::optional<NetworkEnvelope>
NetworkSessionProtocol::message(const NetworkContract *contract,
                                std::string name, std::string target,
                                nlohmann::json data) {
  if (contract == nullptr || !contract->messages.contains(name)) {
    diagnostics_.reject("network message is not declared: " + name);
    return std::nullopt;
  }
  NetworkEnvelope envelope{.kind = NetworkEnvelopeKind::Message,
                           .name = std::move(name),
                           .target = std::move(target),
                           .data = std::move(data)};
  if (const auto *entity = ownership_.find(envelope.target))
    envelope.ownershipGeneration = entity->ownershipGeneration;
  return envelope;
}

std::optional<NetworkEnvelope>
NetworkSessionProtocol::relayMessage(const NetworkContract &contract,
                                     const NetworkEnvelope &envelope,
                                     const std::string &trustedSender) const {
  const auto rule = contract.messages.find(envelope.name);
  if (!ownership_.authoritativeServer() || rule == contract.messages.end() ||
      rule->second.to != NetworkActor::All)
    return std::nullopt;
  NetworkEnvelope relayed = envelope;
  if (!relayed.data.is_object())
    relayed.data = {{"value", relayed.data}};
  relayed.data["_sender_id"] = trustedSender;
  return relayed;
}

nlohmann::json
NetworkSessionProtocol::gameEvent(const NetworkEnvelope &envelope,
                                  const std::string &trustedSender) const {
  std::string sender = trustedSender;
  if (!ownership_.authoritativeServer() && envelope.data.is_object()) {
    const auto forwarded = envelope.data.find("_sender_id");
    if (forwarded != envelope.data.end() && forwarded->is_string())
      sender = forwarded->get<std::string>();
  }
  return {{"name", envelope.name},
          {"sender_id", sender},
          {"target", envelope.target},
          {"data", envelope.data}};
}

std::optional<NetworkEnvelope> NetworkSessionProtocol::spawn(
    const NetworkContract *contract, const std::string &prefabKey,
    const std::string &entityId, std::string owner, nlohmann::json state) {
  if (contract == nullptr || !ownership_.authoritativeServer()) {
    diagnostics_.reject("only the server may spawn declared entities");
    return std::nullopt;
  }
  const auto validation = validateContractReplicatedState(
      *contract, prefabKey, NetworkActor::All, state);
  if (!validation.ok) {
    diagnostics_.reject(validation.error);
    return std::nullopt;
  }
  const OwnershipResult spawned =
      ownership_.spawn(*contract, prefabKey, std::move(owner));
  if (!checkOwnership(spawned))
    return std::nullopt;
  const auto &entity = *spawned.entity;
  nlohmann::json payload = {
      {"network_id", entity.networkId},
      {"prefab_key", prefabKey},
      {"entity_id", entityId},
      {"owner", entity.ownerPeerId},
      {"session_epoch", entity.sessionEpoch},
      {"ownership_generation", entity.ownershipGeneration},
      {"state", std::move(state)}};
  retainedSpawns_[entity.networkId] = payload;
  return spawnEnvelope(payload);
}

void NetworkSessionProtocol::retainOwner(const NetworkOwnedEntity &entity) {
  invalidateEntity(entity.networkId);
  if (auto found = retainedSpawns_.find(entity.networkId);
      found != retainedSpawns_.end()) {
    found->second["owner"] = entity.ownerPeerId;
    found->second["ownership_generation"] = entity.ownershipGeneration;
  }
}

std::optional<NetworkEnvelope>
NetworkSessionProtocol::transfer(const NetworkContract *contract,
                                 const std::string &networkId,
                                 const std::string &owner) {
  if (contract == nullptr || !ownership_.authoritativeServer()) {
    diagnostics_.reject("only the server may transfer ownership");
    return std::nullopt;
  }
  const auto transferred = ownership_.transfer(*contract, networkId, owner);
  if (!checkOwnership(transferred))
    return std::nullopt;
  retainOwner(*transferred.entity);
  return ownerEnvelope(*transferred.entity);
}

std::optional<NetworkEnvelope>
NetworkSessionProtocol::despawn(const NetworkContract *contract,
                                const std::string &networkId) {
  if (contract == nullptr || !ownership_.authoritativeServer()) {
    diagnostics_.reject("only the server may despawn declared entities");
    return std::nullopt;
  }
  const auto removed = ownership_.despawn(networkId);
  if (!checkOwnership(removed))
    return std::nullopt;
  retainedSpawns_.erase(networkId);
  invalidateEntity(networkId);
  return despawnEnvelope(*removed.entity);
}

std::vector<NetworkEnvelope>
NetworkSessionProtocol::disconnectPeer(const NetworkContract &contract,
                                       std::string_view peer) {
  const auto actions = ownership_.disconnectPeer(contract, peer);
  std::vector<NetworkEnvelope> envelopes;
  for (const auto &entity : actions.despawned) {
    retainedSpawns_.erase(entity.networkId);
    invalidateEntity(entity.networkId);
    envelopes.push_back(despawnEnvelope(entity));
  }
  for (const auto &entity : actions.returnedToServer) {
    retainOwner(entity);
    envelopes.push_back(ownerEnvelope(entity));
  }
  for (const auto &entity : actions.awaitingGamePolicy) {
    retainOwner(entity);
    envelopes.push_back(ownerEnvelope(entity));
  }
  return envelopes;
}

std::vector<NetworkEnvelope>
NetworkSessionProtocol::lateJoin(const NetworkContract &contract,
                                 const std::string &peer,
                                 const nlohmann::json &metadata) const {
  std::vector<NetworkEnvelope> envelopes;
  if (!ownership_.authoritativeServer())
    return envelopes;
  envelopes.push_back(
      {.kind = NetworkEnvelopeKind::Session,
       .name = "secure_session",
       .target = "",
       .data = {{"peer_id", peer},
                {"contract_hash", contract.compatibilityHash}}});
  if (!metadata.is_null()) {
    envelopes.push_back({.kind = NetworkEnvelopeKind::Session,
                         .name = "session_start",
                         .target = "",
                         .data = metadata});
  }
  for (const auto &[id, spawn] : retainedSpawns_)
    envelopes.push_back(spawnEnvelope(spawn));
  return envelopes;
}

NetworkGatewayResult NetworkSessionProtocol::reject(NetworkGatewayResult result,
                                                    std::string reason) {
  diagnostics_.reject(reason);
  result.accepted = false;
  result.code = NetworkGatewayRejectCode::InvalidOperation;
  result.internalReason = std::move(reason);
  result.envelope.reset();
  return result;
}

NetworkGatewayResult
NetworkSessionProtocol::receive(const NetworkContract *contract,
                                std::string_view wire,
                                std::string trustedSender, double nowSeconds) {
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(wire.data());
  auto result = gateway_.accept(
      std::span<const std::uint8_t>(bytes, wire.size()),
      {.authoritativeServer = ownership_.authoritativeServer(),
       .trustedSenderPeerId = std::move(trustedSender),
       .localPeerId = localPeerId_.empty() ? "client" : localPeerId_,
       .nowSeconds = nowSeconds,
       .contract = contract,
       .ownership = &ownership_});
  if (!result.accepted) {
    diagnostics_.reject(std::string(networkGatewayRejectCodeName(result.code)));
    return result;
  }
  auto &envelope = *result.envelope;
  const bool handshake = envelope.kind == NetworkEnvelopeKind::Session &&
                         envelope.name == "secure_session";
  if (!ownership_.authoritativeServer() && !ready_ && !handshake)
    return reject(std::move(result), "session handshake is required");
  if (!handshake && envelope.sessionEpoch != ownership_.sessionEpoch())
    return reject(std::move(result),
                  "operation belongs to another session epoch");
  if ((handshake || envelope.kind == NetworkEnvelopeKind::Spawn ||
       envelope.kind == NetworkEnvelopeKind::Ownership ||
       envelope.kind == NetworkEnvelopeKind::Despawn ||
       envelope.kind == NetworkEnvelopeKind::Snapshot) &&
      !envelope.data.is_object())
    return reject(std::move(result), "lifecycle payload must be an object");
  if ((envelope.kind == NetworkEnvelopeKind::Spawn ||
       envelope.kind == NetworkEnvelopeKind::Ownership) &&
      (!envelope.data.contains("owner") || !envelope.data["owner"].is_string()))
    return reject(std::move(result), "lifecycle owner must be a string");
  if (envelope.kind == NetworkEnvelopeKind::Session &&
      envelope.name == "secure_session") {
    if (ready_ || lifecycle_.phase() != NetworkSessionPhase::Connected)
      return reject(std::move(result),
                    "session handshake requires a newly connected peer");
    const auto peer = envelope.data.find("peer_id");
    if (peer == envelope.data.end() || !peer->is_string() ||
        peer->get_ref<const std::string &>().empty() ||
        !ownership_.synchronizeEpoch(envelope.sessionEpoch)) {
      return reject(std::move(result),
                    "secure session epoch or peer is invalid");
    }
    localPeerId_ = peer->get<std::string>();
    diagnostics_.setLocalPeerId(localPeerId_);
    activate();
  } else if (envelope.kind == NetworkEnvelopeKind::Spawn) {
    if (!envelope.data.contains("entity_id") ||
        !envelope.data["entity_id"].is_string() ||
        !envelope.data.contains("state") ||
        !validateContractReplicatedState(*contract, envelope.name,
                                         NetworkActor::All,
                                         envelope.data["state"])
             .ok)
      return reject(std::move(result), "spawn state violates the contract");
    const NetworkOwnedEntity entity{
        .networkId = envelope.target,
        .prefabKey = envelope.name,
        .ownerPeerId = envelope.data.value("owner", "server"),
        .sessionEpoch = envelope.sessionEpoch,
        .ownershipGeneration = envelope.ownershipGeneration};
    if (!checkOwnership(ownership_.applyAuthoritativeSpawn(entity)))
      return reject(std::move(result), "authoritative spawn rejected");
    envelope.data["network_id"] = entity.networkId;
    envelope.data["owner"] = entity.ownerPeerId;
  } else if (envelope.kind == NetworkEnvelopeKind::Ownership) {
    if (!checkOwnership(ownership_.applyAuthoritativeTransfer(
            envelope.target, envelope.data.value("owner", "server"),
            envelope.sessionEpoch, envelope.ownershipGeneration)))
      return reject(std::move(result), "authoritative transfer rejected");
    invalidateEntity(envelope.target);
  } else if (envelope.kind == NetworkEnvelopeKind::Despawn) {
    const auto *entity = ownership_.find(envelope.target);
    const std::string owner =
        entity == nullptr ? std::string{} : entity->ownerPeerId;
    if (!checkOwnership(ownership_.applyAuthoritativeDespawn(
            envelope.target, envelope.sessionEpoch,
            envelope.ownershipGeneration)))
      return reject(std::move(result), "authoritative despawn rejected");
    envelope.data["owner"] = owner;
    invalidateEntity(envelope.target);
  } else if (envelope.kind == NetworkEnvelopeKind::Message &&
             envelope.name == "state_update") {
    if (!envelope.data.is_object())
      return reject(std::move(result), "state update must be an object");
    const auto *entity = ownership_.find(envelope.target);
    const auto state = envelope.data.value("state", nlohmann::json::object());
    const NetworkActor writer =
        entity != nullptr && entity->ownerPeerId == "server"
            ? NetworkActor::Server
            : NetworkActor::Owner;
    if (entity == nullptr || !validateContractReplicatedState(
                                  *contract, entity->prefabKey, writer, state)
                                  .ok)
      return reject(std::move(result), "state update violates field policy");
    envelope.data["network_id"] = envelope.target;
    envelope.data["owner"] = entity->ownerPeerId;
    if (auto found = retainedSpawns_.find(envelope.target);
        found != retainedSpawns_.end())
      found->second["state"] = state;
  } else if (envelope.kind == NetworkEnvelopeKind::Snapshot) {
    const auto snapshot = parseSnapshot(envelope);
    if (!snapshot)
      return reject(std::move(result),
                    "authoritative snapshot payload is invalid");
    prediction_.acceptSnapshot(envelope.target, *snapshot, nowSeconds);
  }
  return result;
}

std::optional<NetworkAuthoritySnapshot>
NetworkSessionProtocol::parseSnapshot(const NetworkEnvelope &envelope) {
  const nlohmann::json &data = envelope.data;
  if (!data.is_object() || !data.contains("tick") || !data.contains("ack") ||
      !data.contains("state") || !data["state"].is_object() ||
      !data["tick"].is_number_unsigned() || !data["ack"].is_number_unsigned())
    return std::nullopt;
  NetworkAuthoritySnapshot snapshot;
  snapshot.sessionEpoch = envelope.sessionEpoch;
  snapshot.ownershipGeneration = envelope.ownershipGeneration;
  snapshot.serverTick = data["tick"].get<std::uint64_t>();
  snapshot.acknowledgedSequence = data["ack"].get<std::uint64_t>();
  if (snapshot.serverTick == 0)
    return std::nullopt;
  snapshot.state = data["state"];
  if (data.contains("marker") && !data["marker"].is_string())
    return std::nullopt;
  const std::string marker = data.value("marker", std::string{"normal"});
  if (marker == "teleport")
    snapshot.marker = NetworkCorrectionMarker::Teleport;
  else if (marker == "reset")
    snapshot.marker = NetworkCorrectionMarker::Reset;
  else if (marker != "normal")
    return std::nullopt;
  if (data.contains("rejected_sequence") &&
      data["rejected_sequence"].is_number_unsigned())
    snapshot.rejectedSequence = data["rejected_sequence"].get<std::uint64_t>();
  if (data.contains("rejection") && data["rejection"].is_string())
    snapshot.rejectionCode = data["rejection"].get<std::string>();
  return snapshot;
}

} // namespace demi::runtime
