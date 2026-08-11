#include "demi/runtime/network/NetworkContract.h"
#include "demi/runtime/network/NetworkFaultSimulator.h"
#include "demi/runtime/network/NetworkMessageGateway.h"
#include "demi/runtime/network/NetworkOwnershipRegistry.h"
#include "demi/runtime/network/NetworkSessionLifecycle.h"

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
  return testContractValidationAndHashing() &&
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
