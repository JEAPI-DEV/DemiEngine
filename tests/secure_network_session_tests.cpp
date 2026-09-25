#include "demi/runtime/network/NetworkContract.h"
#include "demi/runtime/network/NetworkFaultSimulator.h"
#include "demi/runtime/network/NetworkMessageGateway.h"
#include "demi/runtime/network/NetworkOwnershipRegistry.h"
#include "demi/runtime/network/NetworkSessionLifecycle.h"
#include "demi/runtime/network/NetworkSessionProtocol.h"
#include "demi/runtime/network/ReplicatedState.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace demi::runtime;

constexpr const char *ContractJson = R"json({
  "format_version": 1,
  "id": "network-contract://test",
  "limits": {
    "maximum_message_bytes": 512,
    "maximum_messages_per_second": 3,
    "maximum_owned_entities_per_peer": 1,
    "maximum_payload_depth": 5,
    "maximum_payload_elements": 16,
    "maximum_string_bytes": 32
  },
  "replicated_prefabs": {
    "despawn_player": {
      "prefab": "prefab://test/player",
      "spawn_by": "server",
      "ownership": {
        "default": "server",
        "transfer_by": "server",
        "on_disconnect": "despawn"
      },
      "components": {
        "Transform2D": {
          "position": {
            "write_by": "server",
            "visible_to": "all",
            "rate": 20,
            "reliability": "unreliable"
          }
        }
      }
    },
    "persistent_player": {
      "prefab": "prefab://test/persistent_player",
      "spawn_by": "server",
      "ownership": {
        "default": "server",
        "transfer_by": "server",
        "on_disconnect": "return_to_server"
      },
      "components": {}
    }
  },
  "messages": {
    "move_intent": {
      "from": "owner",
      "to": "server",
      "target": "owned_entity",
      "reliability": "unreliable",
      "rate_limit": 2,
      "maximum_bytes": 160
    },
    "match_state": {
      "from": "server",
      "to": "all",
      "target": "none",
      "reliability": "reliable",
      "rate_limit": 2,
      "maximum_bytes": 160
    }
  }
})json";

bool require(const bool condition, const char *message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

NetworkContract contract() {
  auto parsed = parseNetworkContract("test.network.json", ContractJson);
  if (!parsed.contract) {
    for (const auto &diagnostic : parsed.diagnostics)
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    std::abort();
  }
  return *parsed.contract;
}

bool testPerEntityPublicationAndRemoteClock() {
  const auto rules = contract();
  GameNetworkSession diagnostics;
  NetworkSessionProtocol session(diagnostics);
  session.activateHost();
  const auto first = session.spawn(&rules, "persistent_player", "first",
                                   "server", nlohmann::json::object());
  const auto second = session.spawn(&rules, "persistent_player", "second",
                                    "server", nlohmann::json::object());
  if (!require(first && second,
               "Publication fixture failed to spawn two entities."))
    return false;
  const auto &a = first->target;
  const auto &b = second->target;
  using Status = NetworkPublicationStatus;
  if (!require(
          session.publicationStatus(a, 0.125, 0.25) == Status::Waiting &&
              session.publicationStatus(b, 0.0625, 0.25) == Status::Waiting &&
              session.publicationStatus(a, 0.125, 0.25) == Status::Due &&
              session.publicationStatus(b, 0.0625, 0.25) == Status::Waiting &&
              session.publicationStatus(a, 0.0625, 0.25) == Status::Waiting &&
              session.publicationStatus(b, 0.125, 0.25) == Status::Due,
          "Two entities influenced each other's publication cadence."))
    return false;
  if (!require(session.publicationStatus(a, 0.25, 0.25) == Status::Due &&
                   session.publicationStatus(a, 0.1875, 0.25) == Status::Due,
               "Publication discarded the fractional interval remainder."))
    return false;
  if (!require(
          session.publicationStatus(a, 0.125, 0.25) == Status::Waiting &&
              session.transfer(&rules, a, "peer1") &&
              session.publicationStatus(a, 0.125, 0.25) == Status::Rejected &&
              session.transfer(&rules, a, "server") &&
              session.publicationStatus(a, 0.125, 0.25) == Status::Waiting,
          "Ownership transfer retained pacing or permitted the former owner."))
    return false;
  if (!require(session.publicationStatus(b, 0.125, 0.25) == Status::Waiting &&
                   session.transfer(&rules, b, "peer1"),
               "Disconnect pacing fixture failed."))
    return false;
  const auto disconnected = session.disconnectPeer(rules, "peer1");
  if (!require(disconnected.size() == 1 &&
                   session.publicationStatus(b, 0.125, 0.25) ==
                       Status::Waiting &&
                   session.despawn(&rules, a) &&
                   session.publicationStatus(a, 1.0, 0.25) == Status::Rejected,
               "Disconnect/despawn retained entity publication state."))
    return false;
  if (!require(session.publicationStatus(b, -1.0, 0.25) == Status::Rejected &&
                   session.publicationStatus(
                       b, std::numeric_limits<double>::infinity(), 0.25) ==
                       Status::Rejected &&
                   session.publicationStatus(b, 0.125, 0.25) == Status::Due,
               "Invalid timing changed the publication clock."))
    return false;
  session.reset(true);
  if (!require(session.publicationStatus(b, 1.0, 0.25) == Status::Rejected,
               "Session reset retained publication authority."))
    return false;

  NetworkRemoteMotion2D remote{
      .x = 1.0F, .y = 2.0F, .vx = 2.0F, .vy = -2.0F, .receivedAtSeconds = 10.0};
  const auto sampled = remote.position(10.125, 0.25);
  if (!require(sampled == std::pair<float, float>{1.25F, 1.75F} &&
                   remote.position(10.125, 0.25) == sampled &&
                   remote.position(10.125, 0.25) == sampled &&
                   remote.position(11.0, 0.25) ==
                       std::pair<float, float>{1.5F, 1.5F} &&
                   remote.position(9.0, 0.25) ==
                       std::pair<float, float>{1.0F, 2.0F},
               "Remote extrapolation depended on call count or exceeded its "
               "time bound."))
    return false;
  remote.alreadySampled = true;
  return require(remote.position(11.0, 0.25, 0.125) ==
                     std::pair<float, float>{1.0F, 2.0F},
                 "Interpolated snapshot was extrapolated a second time.");
}

bool testNativePredictionCoordinator() {
  auto rules = contract();
  rules.limits.maximumMessageBytes = 4096;
  NetworkOwnershipRegistry serverOwnership(true);
  NetworkOwnershipRegistry clientOwnership(false);
  GameNetworkSession serverDiagnostics;
  GameNetworkSession clientDiagnostics;
  const std::string serverPeer = "server";
  const std::string clientPeer = "peer1";
  NetworkSessionPrediction server(serverOwnership, serverPeer,
                                  serverDiagnostics);
  NetworkSessionPrediction client(clientOwnership, clientPeer,
                                  clientDiagnostics);
  NetworkSessionPrediction::Config config;
  config.inputQueue = {
      .capacity = 8, .futureWindow = 8, .headOfLineTimeoutSeconds = 0.1};
  config.interpolation = {.interpolationDelaySeconds = 0.0,
                          .extrapolationLimitSeconds = 0.1,
                          .capacity = 8};
  config.controller.inputHistoryLimit = 8;
  config.maximumInputsPerTick = 8;
  server.configure(config);
  client.configure(config);
  const auto entity =
      serverOwnership.spawn(rules, "despawn_player", clientPeer);
  if (!require(
          entity.accepted &&
              clientOwnership.synchronizeEpoch(
                  serverOwnership.sessionEpoch()) &&
              clientOwnership.applyAuthoritativeSpawn(*entity.entity).accepted,
          "Prediction ownership fixture failed."))
    return false;
  const std::string id = entity.entity->networkId;
  const auto apply =
      [](const nlohmann::json &state,
         const nlohmann::json &input) -> std::optional<nlohmann::json> {
    return nlohmann::json{
        {"x", state.at("x").get<double>() + input.at("dx").get<double>()}};
  };
  if (!require(
          !server.enable(&rules, id, "move_intent", {{"x", 0.0}}, apply, 0.0) &&
              client.enable(&rules, id, "move_intent", {{"x", 0.0}}, apply,
                            0.0),
          "Prediction enable did not enforce the owning client."))
    return false;
  auto first = client.predict(id, {{"dx", 2.0}});
  auto second = client.predict(id, {{"dx", 3.0}});
  if (!require(first && second && client.state(id)["x"] == 5.0,
               "Native prediction did not apply ordered local inputs."))
    return false;
  first->sessionEpoch = serverOwnership.sessionEpoch();
  if (!require(server.acceptInput(rules, *first, "peer2", 0.0) &&
                   server.takeInputs(id, 0.0).empty(),
               "Forged input reached simulation.") ||
      !require(server.acceptInput(rules, *first, clientPeer, 0.0) &&
                   server.takeInputs(id, 0.0).size() == 1,
               "Authoritative queue did not evaluate accepted input."))
    return false;
  auto published = server.publish(&rules, id, {{"x", 1.0}}, "normal");
  if (!require(published.has_value(), "Native snapshot publication failed."))
    return false;
  published->sessionEpoch = serverOwnership.sessionEpoch();
  const auto snapshot = NetworkSessionProtocol::parseSnapshot(*published);
  if (!require(snapshot.has_value(), "Published snapshot was invalid."))
    return false;
  client.acceptSnapshot(id, *snapshot, 1.0);
  if (!require(client.state(id)["x"] == 4.0 && !client.drainEvents().empty(),
               "Correction did not replay only unacknowledged inputs."))
    return false;
  client.acceptSnapshot(id, *snapshot, 2.0);
  if (!require(client.state(id)["x"] == 4.0 && client.drainEvents().empty(),
               "Duplicate snapshot repeated replay or correction events."))
    return false;
  if (!require(client.rebase(id, {{"x", 10.0}}), "Scene rebase failed."))
    return false;
  const auto rebasedInput = client.predict(id, {{"dx", 1.0}});
  if (!require(rebasedInput && rebasedInput->data["seq"] == 3,
               "Scene rebase restarted the input sequence."))
    return false;
  (void)client.disable(id);
  if (!require(
          client.enable(
              &rules, id, "move_intent", {{"x", 0.0}},
              [](const nlohmann::json &, const nlohmann::json &)
                  -> std::optional<nlohmann::json> { return std::nullopt; },
              3.0) &&
              !client.predict(id, {{"dx", 1.0}}) && client.state(id).is_null(),
          "Failed callback retained partial prediction."))
    return false;
  (void)client.enable(&rules, id, "move_intent", {{"x", 0.0}}, apply, 4.0);
  const auto transferred = serverOwnership.transfer(rules, id, "peer2");
  if (!require(transferred.accepted &&
                   clientOwnership
                       .applyAuthoritativeTransfer(
                           id, "peer2", transferred.entity->sessionEpoch,
                           transferred.entity->ownershipGeneration)
                       .accepted &&
                   !client.predict(id, {{"dx", 1.0}}),
               "Former owner continued predicting."))
    return false;
  client.remove(id);
  client.acceptSnapshot(id, *snapshot, 20.0);
  if (!require(client.remoteState(id, 20.0).is_null(),
               "Old ownership snapshot repopulated interpolation."))
    return false;
  auto remoteSnapshot = *snapshot;
  remoteSnapshot.ownershipGeneration = transferred.entity->ownershipGeneration;
  remoteSnapshot.state = {{"x", 1.0}, {"vx", 1.0}};
  client.acceptSnapshot(id, remoteSnapshot, 20.0);
  if (!require(client.remoteState(id, 20.0)["x"] == 1.0,
               "Interpolation did not use the snapshot receipt clock."))
    return false;
  client.clear();
  return require(client.diagnostics()["channels"].empty(),
                 "Prediction reset retained channels.");
}

bool testNativeSessionProtocol() {
  auto rules = contract();
  rules.limits.maximumMessageBytes = 4096;
  rules.limits.maximumPayloadElements = 256;
  rules.limits.maximumPayloadDepth = 12;
  rules.limits.maximumStringBytes = 256;
  GameNetworkSession serverDiagnostics;
  GameNetworkSession clientDiagnostics;
  NetworkSessionProtocol server(serverDiagnostics);
  NetworkSessionProtocol client(clientDiagnostics);
  server.activateHost();
  client.reset(false);
  client.connected();

  const nlohmann::json state = {{"Transform2D", {{"position", {1.0, 2.0}}}}};
  if (!require(!validateContractReplicatedState(rules, "despawn_player",
                                                NetworkActor::Owner, state)
                    .ok,
               "Spawn validation widened owner write permission.") ||
      !require(!server.spawn(&rules, "despawn_player", "invalid", "peer1",
                             {{"Transform2D", {{"rotation", 1.0}}}}) &&
                   server.ownership().size() == 0,
               "Undeclared spawn state allocated ownership."))
    return false;
  const auto spawned =
      server.spawn(&rules, "despawn_player", "player", "peer1", state);
  if (!require(spawned.has_value(),
               "Native server failed to spawn declared state.") ||
      !require(
          !client.spawn(&rules, "despawn_player", "player", "peer1", state),
          "Native client bypassed server-only spawn."))
    return false;
  const std::string id = spawned->target;
  std::vector<std::string> packets;
  const auto transport = [&](const std::string &wire, bool reliable,
                             std::uint32_t peer) {
    if (!reliable || peer != 2)
      return false;
    packets.push_back(wire);
    return true;
  };
  const auto join = server.lateJoin(rules, "peer2", {{"seed", 42}});
  if (!require(join.size() == 3 && join[0].name == "secure_session" &&
                   join[1].name == "session_start" &&
                   join[2].kind == NetworkEnvelopeKind::Spawn,
               "Late join did not order handshake, metadata and spawn."))
    return false;
  for (const auto &envelope : join) {
    if (!require(server.send(&rules, envelope, transport, 2),
                 "Failed to encode late join.") ||
        !require(client.receive(&rules, packets.back(), "server", 1.0).accepted,
                 "Client rejected native late-join sequence."))
      return false;
  }
  if (!require(
          client.ready() && client.phase() == NetworkSessionPhase::Active &&
              client.localPeerId() == "peer2" &&
              client.ownership().find(id) != nullptr,
          "Handshake failed to establish identity, lifecycle and ownership.") ||
      !require(client.receive(&rules, packets.front(), "server", 2.0).code ==
                   NetworkGatewayRejectCode::Replay,
               "Accepted handshake erased replay protection."))
    return false;

  const auto transfer = server.transfer(&rules, id, "peer2");
  if (!require(transfer && server.send(&rules, *transfer, transport, 2),
               "Native transfer failed.") ||
      !require(client.receive(&rules, packets.back(), "server", 3.0).accepted &&
                   client.ownership().isOwner(id, "peer2"),
               "Transfer did not update the client's owner.") ||
      !require(!client.transfer(&rules, id, "peer1"),
               "Client transferred authority.") ||
      !require(server.lateJoin(rules, "peer3", nullptr).back().data["owner"] ==
                   "peer2",
               "Retained spawn kept the old owner."))
    return false;

  NetworkMessageRule stateRule;
  stateRule.from = NetworkActor::Owner;
  stateRule.to = NetworkActor::All;
  stateRule.target = "entity";
  stateRule.maximumBytes = 4096;
  rules.messages["state_update"] = stateRule;
  auto update = client.message(&rules, "state_update", id, {{"state", state}});
  std::string clientPacket;
  const auto clientTransport = [&](const std::string &wire, bool,
                                   std::uint32_t) {
    clientPacket = wire;
    return true;
  };
  if (!require(update && client.send(&rules, *update, clientTransport),
               "Client update fixture failed.") ||
      !require(!server.receive(&rules, clientPacket, "peer1", 3.1).accepted,
               "Previous owner injected state through the native protocol.") ||
      !require(!server.receive(&rules, clientPacket, "peer2", 3.1).accepted,
               "Owner wrote a server-only field through the native protocol."))
    return false;
  rules.replicatedPrefabs.at("despawn_player")
      .fields.at("Transform2D.position")
      .writeBy = NetworkActor::Owner;
  const nlohmann::json moved = {{"Transform2D", {{"position", {3.0, 4.0}}}}};
  update = client.message(
      &rules, "state_update", id,
      {{"owner", "forged"}, {"network_id", "forged"}, {"state", moved}});
  if (!require(update && client.send(&rules, *update, clientTransport),
               "Owner update fixture failed."))
    return false;
  const auto acceptedUpdate =
      server.receive(&rules, clientPacket, "peer2", 3.2);
  if (!require(
          acceptedUpdate.accepted &&
              acceptedUpdate.envelope->data["owner"] == "peer2" &&
              acceptedUpdate.envelope->data["network_id"] == id &&
              server.lateJoin(rules, "peer3", nullptr).back().data["state"] ==
                  moved,
          "Accepted state did not sanitize identity and update retained "
          "state."))
    return false;

  // The gateway validates framing; the protocol must reject malformed operation
  // payloads before touching ownership or exposing them to a scene adapter.
  NetworkEnvelope malformed{.kind = NetworkEnvelopeKind::Ownership,
                            .ownershipGeneration =
                                transfer->ownershipGeneration + 1,
                            .name = "ownership",
                            .target = id,
                            .data = {{"owner", 7}}};
  if (!require(server.send(&rules, malformed, transport, 2),
               "Malformed fixture was not sent.") ||
      !require(
          !client.receive(&rules, packets.back(), "server", 4.0).accepted &&
              client.ownership().isOwner(id, "peer2"),
          "Malformed owner mutated the client or escaped validation."))
    return false;

  const auto disconnect = server.disconnectPeer(rules, "peer2");
  if (!require(disconnect.size() == 1 &&
                   disconnect[0].kind == NetworkEnvelopeKind::Despawn,
               "Disconnect did not apply the contract despawn policy.") ||
      !require(
          server.send(&rules, disconnect[0], transport, 2) &&
              client.receive(&rules, packets.back(), "server", 5.0).accepted,
          "Client rejected native disconnect cleanup.") ||
      !require(client.ownership().find(id) == nullptr &&
                   server.lateJoin(rules, "peer3", nullptr).size() == 1,
               "Disconnect left ownership or retained spawn state."))
    return false;

  const auto persistent =
      server.spawn(&rules, "persistent_player", "persistent", "peer3",
                   nlohmann::json::object());
  if (!require(persistent.has_value(), "Persistent fixture did not spawn."))
    return false;
  const auto returned = server.disconnectPeer(rules, "peer3");
  if (!require(
          returned.size() == 1 &&
              returned[0].kind == NetworkEnvelopeKind::Ownership &&
              server.ownership().isOwner(persistent->target, "server") &&
              server.lateJoin(rules, "peer4", nullptr).back().data["owner"] ==
                  "server",
          "Disconnect did not retain server ownership for late join."))
    return false;

  const auto message =
      server.message(&rules, "match_state", "", nlohmann::json::array({1, 2}));
  if (!require(message &&
                   server.gameEvent(*message, "peer1")["data"].is_array(),
               "Native event adaptation lost array payloads.") ||
      !require(!server.message(&rules, "undeclared", "", {}),
               "Native service accepted an undeclared message.") ||
      !require(!server.send(&rules, *message,
                            [](const std::string &, bool, std::uint32_t) {
                              return false;
                            }) &&
                   serverDiagnostics.diagnostics().lastError ==
                       "failed to send declared network operation",
               "Transport failure was not reported."))
    return false;

  server.reset(true);
  return require(!server.ready() &&
                     server.phase() == NetworkSessionPhase::Closed &&
                     server.ownership().size() == 0 &&
                     server.lateJoin(rules, "peer4", nullptr).size() == 1,
                 "Reset retained native session state.");
}

bool testContractValidationAndHashing() {
  auto first = parseNetworkContract("test.network.json", ContractJson);
  auto second = parseNetworkContract("test.network.json", ContractJson);
  if (!require(first.contract.has_value() && second.contract.has_value(),
               "Valid network contract did not parse."))
    return false;
  if (!require(first.contract->compatibilityHash ==
                       second.contract->compatibilityHash &&
                   !first.contract->compatibilityHash.empty(),
               "Contract compatibility hash was not deterministic."))
    return false;

  nlohmann::json bad = nlohmann::json::parse(ContractJson);
  bad["replicated_prefabs"]["despawn_player"]["components"]["Sprite"]
     ["texture"] = {{"write_by", "owner"}};
  const auto invalid = parseNetworkContract("bad.network.json", bad.dump());
  if (!require(!invalid.contract.has_value(),
               "Contract exposed a non-replicated reflected field."))
    return false;
  bool found = false;
  for (const auto &diagnostic : invalid.diagnostics)
    found |= diagnostic.code == "NETWORK_CONTRACT_FIELD_NOT_REPLICATED";
  return require(found, "Missing stable invalid-field diagnostic.");
}

bool testServerOnlyOwnershipAndLifecycle() {
  const NetworkContract rules = contract();
  NetworkOwnershipRegistry client(false);
  if (!require(client.spawn(rules, "despawn_player", "peer1").code ==
                   OwnershipRejectCode::NotServer,
               "Client issued a network spawn."))
    return false;

  NetworkOwnershipRegistry server(true);
  const std::uint64_t epoch = server.sessionEpoch();
  const auto player = server.spawn(rules, "despawn_player", "peer1");
  if (!require(player.accepted && player.entity->sessionEpoch == epoch &&
                   player.entity->ownershipGeneration == 1,
               "Server spawn did not assign epoch and generation."))
    return false;
  if (!require(server.spawn(rules, "despawn_player", "peer1").code ==
                   OwnershipRejectCode::EntityLimit,
               "Per-peer owned entity limit was not enforced."))
    return false;
  if (!require(
          server.transfer(rules, player.entity->networkId, "peer2").accepted,
          "Server ownership transfer failed."))
    return false;
  const auto *transferred = server.find(player.entity->networkId);
  if (!require(transferred != nullptr && transferred->ownerPeerId == "peer2" &&
                   transferred->ownershipGeneration == 2,
               "Transfer was not atomic or generation was not advanced."))
    return false;

  const auto persistent = server.spawn(rules, "persistent_player", "peer3");
  if (!require(persistent.accepted, "Persistent entity spawn failed."))
    return false;
  const auto despawnActions = server.disconnectPeer(rules, "peer2");
  const auto persistentActions = server.disconnectPeer(rules, "peer3");
  if (!require(despawnActions.despawned.size() == 1 &&
                   persistentActions.returnedToServer.size() == 1 &&
                   server.size() == 1,
               "Disconnect policies were not applied before callbacks."))
    return false;
  if (!require(server.find(persistent.entity->networkId)->ownerPeerId ==
                   "server",
               "Disconnected ownership was not revoked to server."))
    return false;

  server.reset(true);
  return require(server.sessionEpoch() != epoch && server.size() == 0,
                 "Session reset retained stale ownership.");
}

NetworkEnvelope message(const NetworkOwnershipRegistry &ownership,
                        const std::string &name, const std::string &target,
                        const std::uint64_t sequence,
                        nlohmann::json data = {{"x", 1.0}, {"y", 0.0}}) {
  return {.kind = NetworkEnvelopeKind::Message,
          .sessionEpoch = ownership.sessionEpoch(),
          .ownershipGeneration =
              target.empty() || ownership.find(target) == nullptr
                  ? 0
                  : ownership.find(target)->ownershipGeneration,
          .sequence = sequence,
          .name = name,
          .target = target,
          .data = std::move(data)};
}

bool testGatewayPermissionReplayAndRateLimits() {
  const NetworkContract rules = contract();
  NetworkOwnershipRegistry ownership(true);
  const auto player = ownership.spawn(rules, "despawn_player", "peer1");
  NetworkMessageGateway gateway;
  const auto context = [&](const std::string &peer, const double time) {
    return NetworkGatewayContext{.authoritativeServer = true,
                                 .trustedSenderPeerId = peer,
                                 .nowSeconds = time,
                                 .contract = &rules,
                                 .ownership = &ownership};
  };
  auto bytes = gateway.encode(
      rules, message(ownership, "move_intent", player.entity->networkId, 1));
  if (!require(gateway.accept(bytes, context("peer1", 0.0)).accepted,
               "Owner intent was rejected."))
    return false;
  if (!require(gateway.accept(bytes, context("peer1", 0.0)).code ==
                   NetworkGatewayRejectCode::Replay,
               "Duplicated sequence was not rejected."))
    return false;
  auto forged = gateway.encode(
      rules, message(ownership, "move_intent", player.entity->networkId, 1));
  if (!require(gateway.accept(forged, context("peer2", 0.0)).code ==
                   NetworkGatewayRejectCode::UnauthorizedTarget,
               "Authenticated transport peer bypassed target ownership."))
    return false;
  auto undeclared = gateway.encode(
      rules, message(ownership, "god_mode", player.entity->networkId, 2));
  if (!require(gateway.accept(undeclared, context("peer1", 0.0)).code ==
                   NetworkGatewayRejectCode::UnknownMessage,
               "Undeclared message reached gameplay."))
    return false;
  for (std::uint64_t sequence = 2; sequence <= 3; ++sequence) {
    auto next =
        gateway.encode(rules, message(ownership, "move_intent",
                                      player.entity->networkId, sequence));
    const auto result = gateway.accept(next, context("peer1", 0.1));
    if (sequence == 2 && !require(result.accepted, "Valid rate slot failed."))
      return false;
    if (sequence == 3 &&
        !require(result.code == NetworkGatewayRejectCode::RateLimited,
                 "Per-message rate limit was not enforced."))
      return false;
  }
  auto afterWindow = gateway.encode(
      rules, message(ownership, "move_intent", player.entity->networkId, 4));
  return require(gateway.accept(afterWindow, context("peer1", 1.1)).accepted,
                 "Rate window did not recover deterministically.");
}

bool testGatewayMalformedAndBoundedInputs() {
  const NetworkContract rules = contract();
  NetworkOwnershipRegistry ownership(true);
  const auto player = ownership.spawn(rules, "despawn_player", "peer1");
  const NetworkGatewayContext context{.authoritativeServer = true,
                                      .trustedSenderPeerId = "peer1",
                                      .nowSeconds = 0.0,
                                      .contract = &rules,
                                      .ownership = &ownership};
  NetworkMessageGateway gateway;
  const std::vector<std::uint8_t> tiny{'D', 'N'};
  if (!require(gateway.accept(tiny, context).code ==
                   NetworkGatewayRejectCode::Truncated,
               "Truncated fixed header was not rejected."))
    return false;

  auto valid = gateway.encode(
      rules, message(ownership, "move_intent", player.entity->networkId, 1));
  std::string invalidTarget(valid.begin(), valid.end());
  const std::string encodedTarget =
      "\"target\":\"" + player.entity->networkId + "\"";
  const auto targetOffset =
      invalidTarget.find(encodedTarget, NetworkMessageGateway::HeaderBytes);
  if (!require(targetOffset != std::string::npos,
               "Target fixture was not encoded."))
    return false;
  std::string nullTarget = "\"target\":null";
  nullTarget.append(encodedTarget.size() - nullTarget.size(), ' ');
  invalidTarget.replace(targetOffset, encodedTarget.size(), nullTarget);
  const std::vector<std::uint8_t> invalidTargetBytes(invalidTarget.begin(),
                                                     invalidTarget.end());
  if (!require(gateway.accept(invalidTargetBytes, context).code ==
                   NetworkGatewayRejectCode::InvalidJson,
               "Non-string target reached protocol dispatch."))
    return false;
  auto badMagic = valid;
  badMagic[0] = 'X';
  if (!require(gateway.accept(badMagic, context).code ==
                   NetworkGatewayRejectCode::BadMagic,
               "Bad envelope magic was not rejected."))
    return false;
  auto invalidUtf8 = valid;
  const auto nameByte =
      std::find(invalidUtf8.begin() + NetworkMessageGateway::HeaderBytes,
                invalidUtf8.end(), static_cast<std::uint8_t>('m'));
  if (!require(nameByte != invalidUtf8.end(),
               "Malformed UTF-8 test could not locate payload text."))
    return false;
  *nameByte = 0xff;
  if (!require(gateway.accept(invalidUtf8, context).code ==
                   NetworkGatewayRejectCode::InvalidJson,
               "Invalid UTF-8 reached message dispatch."))
    return false;
  auto trailing = valid;
  trailing.push_back(0);
  if (!require(gateway.accept(trailing, context).code ==
                   NetworkGatewayRejectCode::Truncated,
               "Trailing bytes were accepted."))
    return false;

  nlohmann::json deep = 1;
  for (int index = 0; index < 8; ++index)
    deep = nlohmann::json::array({deep});
  auto deepBytes =
      gateway.encode(rules, message(ownership, "move_intent",
                                    player.entity->networkId, 2, deep));
  if (!require(gateway.accept(deepBytes, context).code ==
                   NetworkGatewayRejectCode::ExcessiveDepth,
               "Over-depth payload was accepted."))
    return false;

  auto longString = gateway.encode(rules, message(ownership, "move_intent",
                                                  player.entity->networkId, 2,
                                                  std::string(64, 'x')));
  if (!require(gateway.accept(longString, context).code ==
                   NetworkGatewayRejectCode::StringTooLong,
               "Overlong string was accepted."))
    return false;

  auto wrongContract = rules;
  wrongContract.compatibilityHash = "fnv1a64:0000000000000001";
  auto wrongBytes =
      gateway.encode(wrongContract, message(ownership, "move_intent",
                                            player.entity->networkId, 2));
  if (!require(gateway.accept(wrongBytes, context).code ==
                   NetworkGatewayRejectCode::ContractMismatch,
               "Mismatched contract was accepted."))
    return false;

  ownership.reset(true);
  return require(gateway.accept(valid, context).code ==
                     NetworkGatewayRejectCode::StaleEpoch,
                 "Stale session epoch was accepted after reset.");
}

bool testGatewaySchemaAndOwnershipGeneration() {
  NetworkContract rules = contract();
  auto &intent = rules.messages.at("move_intent");
  intent.schema = "asset://schemas/move_intent";
  intent.schemaDocument = {
      {"format_version", 1},
      {"type", "object"},
      {"required", {"x", "y"}},
      {"properties",
       {{"x", {{"type", "number"}, {"minimum", -1}, {"maximum", 1}}},
        {"y", {{"type", "number"}, {"minimum", -1}, {"maximum", 1}}}}},
      {"additionalProperties", false}};
  NetworkOwnershipRegistry ownership(true);
  const auto player = ownership.spawn(rules, "despawn_player", "peer1");
  const NetworkGatewayContext context{.authoritativeServer = true,
                                      .trustedSenderPeerId = "peer1",
                                      .nowSeconds = 0.0,
                                      .contract = &rules,
                                      .ownership = &ownership};
  NetworkMessageGateway gateway;
  auto missing =
      gateway.encode(rules, message(ownership, "move_intent",
                                    player.entity->networkId, 1, {{"x", 0.5}}));
  if (!require(gateway.accept(missing, context).code ==
                   NetworkGatewayRejectCode::SchemaViolation,
               "Schema accepted a missing required field."))
    return false;
  auto extra = gateway.encode(
      rules, message(ownership, "move_intent", player.entity->networkId, 2,
                     {{"x", 0.5}, {"y", 0.0}, {"admin", true}}));
  if (!require(gateway.accept(extra, context).code ==
                   NetworkGatewayRejectCode::SchemaViolation,
               "Schema accepted an undeclared field."))
    return false;
  auto outOfRange = gateway.encode(rules, message(ownership, "move_intent",
                                                  player.entity->networkId, 3,
                                                  {{"x", 4.0}, {"y", 0.0}}));
  if (!require(gateway.accept(outOfRange, context).code ==
                   NetworkGatewayRejectCode::SchemaViolation,
               "Schema accepted an out-of-range number."))
    return false;

  auto stale = message(ownership, "move_intent", player.entity->networkId, 4);
  const auto staleBytes = gateway.encode(rules, stale);
  if (!require(
          ownership.transfer(rules, player.entity->networkId, "peer2").accepted,
          "Ownership transfer setup failed."))
    return false;
  NetworkGatewayContext oldOwner = context;
  oldOwner.trustedSenderPeerId = "peer1";
  return require(gateway.accept(staleBytes, oldOwner).code ==
                     NetworkGatewayRejectCode::StaleGeneration,
                 "Late state from the previous owner generation was accepted.");
}

bool testClientRejectsLifecycleFromPeers() {
  const NetworkContract rules = contract();
  NetworkOwnershipRegistry ownership(false);
  NetworkMessageGateway gateway;
  NetworkEnvelope lifecycle{.kind = NetworkEnvelopeKind::Spawn,
                            .sessionEpoch = ownership.sessionEpoch(),
                            .ownershipGeneration = 1,
                            .sequence = 1,
                            .name = "despawn_player",
                            .target = "net:1:1",
                            .data = nlohmann::json::object()};
  const auto bytes = gateway.encode(rules, lifecycle);
  NetworkGatewayContext peer{.trustedSenderPeerId = "peer7",
                             .contract = &rules,
                             .ownership = &ownership};
  if (!require(gateway.accept(bytes, peer).code ==
                   NetworkGatewayRejectCode::UnauthorizedSender,
               "Peer-issued lifecycle operation was accepted."))
    return false;
  gateway.reset();
  peer.trustedSenderPeerId = "server";
  if (!require(gateway.accept(bytes, peer).accepted,
               "Client rejected authoritative lifecycle operation."))
    return false;

  gateway.reset();
  NetworkEnvelope handshake{.kind = NetworkEnvelopeKind::Session,
                            .sessionEpoch = ownership.sessionEpoch() + 9,
                            .sequence = 1,
                            .name = "secure_session",
                            .data = {{"peer_id", "peer7"}}};
  const auto handshakeBytes = gateway.encode(rules, handshake);
  if (!require(gateway.accept(handshakeBytes, peer).accepted,
               "Client could not accept a trusted next-epoch handshake."))
    return false;
  gateway.reset();
  peer.trustedSenderPeerId = "peer8";
  return require(gateway.accept(handshakeBytes, peer).code ==
                     NetworkGatewayRejectCode::StaleEpoch,
                 "Untrusted peer advanced the client session epoch.");
}

bool testDeterministicFaultInjectionIsBounded() {
  NetworkFaultSimulator simulator({.dropEvery = 3,
                                   .duplicateEvery = 2,
                                   .delayTicks = 2,
                                   .reorderWindow = 3,
                                   .maximumQueuedPackets = 5});
  const std::vector<std::uint8_t> bytes{1, 2, 3};
  if (!require(simulator.submit(1, bytes, 10) &&
                   simulator.submit(2, bytes, 10) &&
                   simulator.submit(3, bytes, 10),
               "Fault simulator rejected packets below its bound."))
    return false;
  if (!require(simulator.drain(11).empty(),
               "Delayed packets were delivered too early."))
    return false;
  const auto delivered = simulator.drain(12);
  if (!require(delivered.size() == 3 && delivered[0].id == 2 &&
                   delivered[1].id == 2 && delivered[2].id == 1,
               "Loss, duplication, or deterministic reordering differed."))
    return false;
  if (!require(simulator.stats().submitted == 3 &&
                   simulator.stats().dropped == 1 &&
                   simulator.stats().duplicated == 1,
               "Fault counters were not deterministic."))
    return false;

  NetworkFaultSimulator bounded(
      {.duplicateEvery = 1, .maximumQueuedPackets = 1});
  if (!require(!bounded.submit(7, bytes, 0) && bounded.queued() == 0 &&
                   bounded.stats().rejectedAtCapacity == 1,
               "Fault simulator exceeded its queue budget atomically."))
    return false;
  bounded.reset();
  return require(bounded.stats().submitted == 0 && bounded.queued() == 0,
                 "Fault simulator reset retained packets or counters.");
}

bool testLifecycleAndReconnectRevocation() {
  NetworkSessionLifecycle lifecycle;
  if (!require(!lifecycle.transition(NetworkSessionPhase::Active),
               "Session skipped authentication and readiness."))
    return false;
  if (!require(lifecycle.transition(NetworkSessionPhase::Connected) &&
                   lifecycle.transition(NetworkSessionPhase::Authenticated) &&
                   lifecycle.transition(NetworkSessionPhase::Ready) &&
                   lifecycle.transition(NetworkSessionPhase::Active),
               "Valid session lifecycle was rejected."))
    return false;
  if (!require(!lifecycle.transition(NetworkSessionPhase::Connected),
               "Active session returned to connected without reset."))
    return false;

  const NetworkContract rules = contract();
  NetworkOwnershipRegistry ownership(true);
  const auto player = ownership.spawn(rules, "despawn_player", "peer1");
  ReconnectLeaseStore leases;
  const std::string valid = leases.issue("peer1", ownership, 100, 50);
  if (!require(leases.consume(valid, ownership, 150).accepted,
               "Valid reconnect lease was rejected at its inclusive expiry."))
    return false;
  if (!require(leases.consume(valid, ownership, 150).code ==
                   ReconnectRejectCode::UnknownToken,
               "Reconnect token was reusable."))
    return false;

  const std::string expired = leases.issue("peer1", ownership, 200, 10);
  if (!require(leases.consume(expired, ownership, 211).code ==
                   ReconnectRejectCode::Expired,
               "Expired reconnect lease was accepted."))
    return false;
  const std::string saturated = leases.issue(
      "peer1", ownership, std::numeric_limits<std::uint64_t>::max() - 2, 20);
  if (!require(leases
                   .consume(saturated, ownership,
                            std::numeric_limits<std::uint64_t>::max())
                   .accepted,
               "Reconnect expiry overflowed instead of saturating."))
    return false;
  const std::string transferred = leases.issue("peer1", ownership, 300, 100);
  if (!require(
          ownership.transfer(rules, player.entity->networkId, "peer2").accepted,
          "Reconnect transfer setup failed."))
    return false;
  if (!require(leases.consume(transferred, ownership, 301).code ==
                   ReconnectRejectCode::StaleOwnership,
               "Old owner reclaimed an entity after transfer."))
    return false;

  const std::string staleEpoch = leases.issue("peer2", ownership, 400, 100);
  ownership.reset(true);
  if (!require(leases.consume(staleEpoch, ownership, 401).code ==
                   ReconnectRejectCode::StaleEpoch,
               "Reconnect survived a session epoch reset."))
    return false;
  const std::string resetLease = leases.issue("peer3", ownership, 500, 100);
  (void)resetLease;
  leases.reset();
  lifecycle.reset();
  return require(leases.size() == 0 &&
                     lifecycle.phase() == NetworkSessionPhase::Closed,
                 "Lifecycle reset retained reconnect capabilities.");
}

} // namespace

int main() {
  return testPerEntityPublicationAndRemoteClock() &&
                 testNativePredictionCoordinator() &&
                 testNativeSessionProtocol() &&
                 testContractValidationAndHashing() &&
                 testServerOnlyOwnershipAndLifecycle() &&
                 testGatewayPermissionReplayAndRateLimits() &&
                 testGatewayMalformedAndBoundedInputs() &&
                 testGatewaySchemaAndOwnershipGeneration() &&
                 testClientRejectsLifecycleFromPeers() &&
                 testDeterministicFaultInjectionIsBounded() &&
                 testLifecycleAndReconnectRevocation()
             ? 0
             : 1;
}
