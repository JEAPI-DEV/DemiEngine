#include "demi/runtime/scripting/bindings/LuaNetworkSessionBindings.h"
#include "demi/runtime/network/GameNetworkSession.h"
#include "demi/runtime/network/NetworkMessageGateway.h"
#include "demi/runtime/network/NetworkOwnershipRegistry.h"
#include "demi/runtime/network/NetworkSessionLifecycle.h"
#include "demi/runtime/network/ReplicatedState.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

namespace {

struct NetworkSessionClaimObject {
  bool pending = false;
  sol::function onRemoved;
  sol::function onClaimedLocal;
  sol::function canClaim;
};

struct NetworkSessionRemote {
  std::string senderId;
  float x = 0.0F;
  float y = 0.0F;
  float vx = 0.0F;
  float vy = 0.0F;
  float age = 0.0F;
};

struct NetworkSessionState {
  float sendInterval = 1.0F / 60.0F;
  float extrapolationLimit = 0.10F;
  float initialPrediction = 0.025F;
  std::uint8_t channel = 1;
  std::uint16_t defaultPort = 39420;
  std::uint32_t maxPeers = 8;
  std::string certificate;
  std::string privateKey;
  std::string trustedCertificate;
  std::string serverName;
  float accumulator = 0.0F;
  std::string localPeerId;
  GameNetworkSession game;
  NetworkOwnershipRegistry ownership;
  NetworkMessageGateway gateway;
  NetworkSessionLifecycle lifecycle;
  ReconnectLeaseStore reconnects;
  std::uint64_t outgoingSequence = 1;
  bool secureReady = false;
  std::unordered_map<std::string, nlohmann::json> retainedSpawns;
  Color localColor = {1.0F, 1.0F, 1.0F, 1.0F};
  sol::object sessionMetadata = sol::nil;
  sol::table remotePrefab;
  std::unordered_map<std::string, NetworkSessionRemote> remotes;
  std::unordered_map<std::string, NetworkSessionClaimObject> claimObjects;
  std::unordered_map<std::string, std::string> claimedObjects;
  std::unordered_map<std::string, std::string> localNetworkEntities;
  std::vector<nlohmann::json> gameEvents;
};

std::string networkSessionSenderId(LuaScriptHost &host,
                                   const NetworkSessionState &session) {
  if (host.networkIsHost()) {
    return host.networkContract() != nullptr ? "server" : "host";
  }
  return session.localPeerId.empty() ? "client" : session.localPeerId;
}

std::string networkSessionRemoteId(const sol::table snapshot) {
  return "net_" + snapshot.get_or("sender_id", std::string{}) + "_" +
         snapshot.get_or("entity_id", std::string{});
}

std::string networkSessionRemoteId(const std::string &owner,
                                   const std::string &networkId) {
  return "net_" + owner + "_" + networkId;
}

void networkSessionApplySnapshot(LuaScriptHost &host,
                                 NetworkSessionState &session,
                                 sol::table snapshot);
bool networkSessionSendMessage(lua_State *state, LuaScriptHost &host,
                               const std::string &type, sol::object payload,
                               bool reliable, std::uint32_t peerId,
                               std::uint8_t channel);

void networkSessionReset(LuaScriptHost &host, NetworkSessionState &session,
                         const bool clearRemoteEntities) {
  if (clearRemoteEntities) {
    for (const auto &[ghostId, _] : session.remotes) {
      (void)host.destroyEntity(ghostId);
    }
  }
  session.localPeerId.clear();
  session.game.reset(host.networkIsHost());
  session.ownership.reset(host.networkIsHost());
  session.gateway.reset();
  session.outgoingSequence = 1;
  session.secureReady = false;
  session.retainedSpawns.clear();
  session.lifecycle.reset();
  session.reconnects.reset();
  session.accumulator = 0.0F;
  session.remotes.clear();
  session.claimObjects.clear();
  session.claimedObjects.clear();
  session.localNetworkEntities.clear();
  session.gameEvents.clear();
  session.sessionMetadata = sol::object{};
}

sol::table networkSessionDiagnostics(lua_State *state, LuaScriptHost &host,
                                     NetworkSessionState &session) {
  sol::state_view lua(state);
  session.game.setLatency(host.networkLatencyMs());
  const NetworkDiagnostics &diagnostics = session.game.diagnostics();
  sol::table result = lua.create_table();
  result["mode"] = host.networkIsHost()
                       ? "host"
                       : (host.networkIsConnected() ? "client" : "offline");
  result["local_peer_id"] = networkSessionSenderId(host, session);
  result["connected"] = host.networkIsConnected();
  result["secure"] = host.networkIsSecure();
  result["latency_ms"] = session.game.latencyMs();
  result["connected_peers"] = diagnostics.connectedPeers;
  result["sent_messages"] = diagnostics.sentMessages;
  result["received_messages"] = diagnostics.receivedMessages;
  result["rejected_messages"] = diagnostics.rejectedMessages;
  result["last_error"] = !diagnostics.lastError.empty()
                             ? diagnostics.lastError
                             : host.networkSecurityError();
  result["session_epoch"] = session.ownership.sessionEpoch();
  result["contract_hash"] = host.networkContract() == nullptr
                                ? std::string{}
                                : host.networkContract()->compatibilityHash;
  result["secure_accepted_messages"] = session.gateway.counters().accepted;
  std::uint64_t secureRejected = 0;
  for (const auto &[unused, count] : session.gateway.counters().rejected) {
    (void)unused;
    secureRejected += count;
  }
  result["secure_rejected_messages"] = secureRejected;
  result["secure_ready"] =
      host.networkContract() == nullptr || session.secureReady;
  result["phase"] =
      std::string(networkSessionPhaseName(session.lifecycle.phase()));
  return result;
}

bool networkSessionSendGameMessage(lua_State *state, LuaScriptHost &host,
                                   NetworkSessionState &session,
                                   const std::string &type,
                                   const sol::object payload,
                                   const bool reliable = true,
                                   const std::uint32_t peerId = 0,
                                   const std::uint8_t channel = 0) {
  const bool sent = networkSessionSendMessage(state, host, type, payload,
                                              reliable, peerId, channel);
  if (sent)
    session.game.messageSent();
  else
    session.game.reject("failed to send " + type);
  return sent;
}

sol::table networkSessionStatePayload(lua_State *state, LuaScriptHost &host,
                                      NetworkSessionState &session,
                                      const std::string &networkId,
                                      const std::string &entityId) {
  sol::state_view lua(state);
  sol::table payload = lua.create_table();
  sol::table color = lua.create_table();
  color[1] = session.localColor.r;
  color[2] = session.localColor.g;
  color[3] = session.localColor.b;
  color[4] = session.localColor.a;
  payload["network_id"] = networkId;
  payload["entity_id"] = entityId;
  payload["owner"] = networkSessionSenderId(host, session);
  payload["color"] = color;
  if (const auto json = host.captureEntityReplicatedState(entityId)) {
    payload["state"] = jsonToLuaObject(state, nlohmann::json::parse(*json));
  } else {
    payload["state"] = lua.create_table();
  }
  return payload;
}

bool networkSessionCreateOrApplyRemote(lua_State *state, LuaScriptHost &host,
                                       NetworkSessionState &session,
                                       const sol::table payload) {
  const std::string owner = payload.get_or("owner", std::string{});
  const std::string networkId = payload.get_or("network_id", std::string{});
  const sol::object stateObject = payload["state"];
  if (owner.empty() || networkId.empty() || !stateObject.is<sol::table>())
    return false;
  if (owner == networkSessionSenderId(host, session))
    return true;

  const nlohmann::json replicatedJson = luaObjectToJson(stateObject);
  const ReplicatedStateResult validation =
      validateReplicatedState(replicatedJson);
  if (!validation.ok) {
    session.game.reject(validation.error);
    return false;
  }
  const std::string ghostId = networkSessionRemoteId(owner, networkId);
  const sol::table replicated = stateObject.as<sol::table>();
  const sol::object transformObject = replicated["Transform2D"];
  sol::table transform = transformObject.is<sol::table>()
                             ? transformObject.as<sol::table>()
                             : sol::state_view(state).create_table();
  sol::table snapshot = sol::state_view(state).create_table();
  snapshot["sender_id"] = owner;
  snapshot["entity_id"] = networkId;
  if (transform["position"].is<sol::table>()) {
    const sol::table position = transform["position"];
    snapshot["x"] = position.get_or(1, 0.0F);
    snapshot["y"] = position.get_or(2, 0.0F);
  } else {
    snapshot["x"] = 0.0F;
    snapshot["y"] = 0.0F;
  }
  snapshot["vx"] = 0.0F;
  snapshot["vy"] = 0.0F;
  if (payload["color"].is<sol::table>())
    snapshot["color"] = payload["color"];
  const sol::object bodyObject = replicated["Rigidbody2D"];
  if (bodyObject.is<sol::table>()) {
    const sol::table body = bodyObject.as<sol::table>();
    const sol::object velocityObject = body["velocity"];
    if (velocityObject.is<sol::table>()) {
      const sol::table velocity = velocityObject.as<sol::table>();
      snapshot["vx"] = velocity.get_or(1, 0.0F);
      snapshot["vy"] = velocity.get_or(2, 0.0F);
    }
  }
  networkSessionApplySnapshot(host, session, snapshot);
  const std::string error =
      host.applyEntityReplicatedState(ghostId, replicatedJson.dump());
  if (!error.empty()) {
    session.game.reject(error);
    return false;
  }
  return true;
}

sol::table networkSessionClaimSyncPayload(lua_State *state,
                                          const NetworkSessionState &session) {
  sol::state_view lua(state);
  sol::table payload = lua.create_table();
  sol::table claims = lua.create_table();
  int index = 1;
  for (const auto &[objectId, collectorId] : session.claimedObjects) {
    sol::table claim = lua.create_table();
    claim["object_id"] = objectId;
    claim["collector_id"] = collectorId;
    claims[index++] = claim;
  }
  payload["claims"] = claims;
  return payload;
}

bool networkSessionSendMessage(lua_State *state, LuaScriptHost &host,
                               const std::string &type,
                               const sol::object payload,
                               const bool reliable = true,
                               const std::uint32_t peerId = 0,
                               const std::uint8_t channel = 0) {
  (void)state;
  return host.networkSend(
      encodeNetworkMessage(type, sol::optional<sol::object>(payload)), reliable,
      channel, peerId);
}

bool networkSessionSendClaimSync(lua_State *state, LuaScriptHost &host,
                                 const NetworkSessionState &session,
                                 const std::uint32_t peerId = 0) {
  if (!host.networkAvailable() || !host.networkIsHost()) {
    return false;
  }
  sol::object payload =
      sol::make_object(state, networkSessionClaimSyncPayload(state, session));
  return networkSessionSendMessage(state, host, "claim_once_sync", payload,
                                   true, peerId, 0);
}

void networkSessionApplySnapshot(LuaScriptHost &host,
                                 NetworkSessionState &session,
                                 const sol::table snapshot) {
  const std::string senderId = snapshot.get_or("sender_id", std::string{});
  if (senderId.empty() || senderId == networkSessionSenderId(host, session) ||
      !snapshot["x"].valid() || !snapshot["y"].valid()) {
    return;
  }

  const std::string ghostId = networkSessionRemoteId(snapshot);
  const bool needsCreate = !session.remotes.contains(ghostId) ||
                           !host.findEntityId(ghostId).has_value();
  if (needsCreate) {
    if (session.remotePrefab.valid()) {
      Entity entity;
      entity.id = ghostId;
      entity.name =
          session.remotePrefab.get_or("name", std::string("Network Ghost"));
      entity.setComponent<Transform2DComponent>(Transform2DComponent{
          .parent = session.remotePrefab.get_or("parent", std::string{}),
          .position = Vec2{.x = snapshot.get_or("x", 0.0F),
                           .y = snapshot.get_or("y", 0.0F)},
          .rotation = session.remotePrefab.get_or("rotation", 0.0F),
          .scale = luaVec2Field(session.remotePrefab, "scale", {1.0F, 1.0F}),
      });
      entity.setComponent<SpriteComponent>(SpriteComponent{
          .texture = session.remotePrefab.get_or("texture", std::string{}),
          .shape =
              session.remotePrefab.get_or("shape", std::string("rectangle")),
          .layer = session.remotePrefab.get_or("layer", std::string("network")),
          .sortingOrder = session.remotePrefab.get_or("sorting_order", 0),
          .size = luaVec2Field(session.remotePrefab, "size"),
          .pivot = luaVec2Field(session.remotePrefab, "pivot", {0.5F, 0.5F}),
          .material = session.remotePrefab.get_or("material", std::string{}),
          .color = luaColorField(snapshot, "color",
                                 luaColorField(session.remotePrefab, "color")),
      });
      (void)host.createEntity(std::move(entity));
    }
    session.remotes[ghostId] = NetworkSessionRemote{.senderId = senderId};
  }

  NetworkSessionRemote &remote = session.remotes[ghostId];
  remote.senderId = senderId;
  remote.x = snapshot.get_or("x", 0.0F);
  remote.y = snapshot.get_or("y", 0.0F);
  remote.vx = snapshot.get_or("vx", 0.0F);
  remote.vy = snapshot.get_or("vy", 0.0F);
  remote.age = 0.0F;
  if (snapshot["color"].is<sol::table>()) {
    (void)host.setEntitySpriteColor(
        ghostId, luaColorField(snapshot, "color",
                               luaColorField(session.remotePrefab, "color")));
  }
  (void)host.setEntityPosition(
      ghostId, remote.x + remote.vx * session.initialPrediction,
      remote.y + remote.vy * session.initialPrediction);
}

bool networkSessionApplyClaimOnce(lua_State *state, LuaScriptHost &host,
                                  NetworkSessionState &session,
                                  const std::string &id,
                                  const std::string &collectorId,
                                  const bool broadcast,
                                  const sol::object claim) {
  if (id.empty() || session.claimedObjects.contains(id)) {
    return false;
  }

  auto object = session.claimObjects.find(id);
  if (broadcast && object != session.claimObjects.end() &&
      object->second.canClaim.valid()) {
    const sol::protected_function canClaim = object->second.canClaim;
    const sol::protected_function_result result =
        canClaim(id, collectorId, claim);
    if (!result.valid() || !result.get<bool>()) {
      return false;
    }
  }

  session.claimedObjects[id] = collectorId;
  if (object != session.claimObjects.end()) {
    object->second.pending = false;
    if (object->second.onRemoved.valid()) {
      const sol::protected_function onRemoved = object->second.onRemoved;
      (void)onRemoved(id, collectorId);
    } else {
      (void)host.destroyEntity(id);
    }
    if (collectorId == networkSessionSenderId(host, session) &&
        object->second.onClaimedLocal.valid()) {
      const sol::protected_function onClaimedLocal =
          object->second.onClaimedLocal;
      (void)onClaimedLocal(id, collectorId);
    }
  }

  if (broadcast && host.networkAvailable() && host.networkIsHost()) {
    sol::state_view lua(state);
    sol::table payload = lua.create_table();
    payload["object_id"] = id;
    payload["collector_id"] = collectorId;
    networkSessionSendMessage(state, host, "claim_once_claimed",
                              sol::make_object(state, payload), true, 0, 0);
  }
  return true;
}

bool networkSessionSendEnvelope(LuaScriptHost &host,
                                NetworkSessionState &session,
                                NetworkEnvelope envelope,
                                const std::uint32_t peerId = 0) {
  const NetworkContract *contract = host.networkContract();
  if (contract == nullptr) {
    session.game.reject("project does not declare a network_contract asset");
    return false;
  }
  envelope.sessionEpoch = session.ownership.sessionEpoch();
  envelope.sequence = session.outgoingSequence++;
  const std::vector<std::uint8_t> bytes =
      session.gateway.encode(*contract, envelope);
  const bool reliable = envelope.kind != NetworkEnvelopeKind::Message ||
                        (contract->messages.contains(envelope.name) &&
                         contract->messages.at(envelope.name).reliability ==
                             NetworkReliability::Reliable);
  const std::string wire(reinterpret_cast<const char *>(bytes.data()),
                         bytes.size());
  // Every contract envelope uses one ordered channel. Sequence numbers are
  // session-wide, so splitting lifecycle and gameplay across channels would
  // let a later packet overtake an earlier one and create a false replay.
  if (!host.networkSend(wire, reliable, 0, peerId)) {
    session.game.reject("failed to send declared network operation");
    return false;
  }
  session.game.messageSent();
  return true;
}

bool networkSessionSendRetainedSpawn(LuaScriptHost &host,
                                     NetworkSessionState &session,
                                     const nlohmann::json &spawn,
                                     const std::uint32_t peerId = 0) {
  NetworkEnvelope envelope;
  envelope.kind = NetworkEnvelopeKind::Spawn;
  envelope.name = spawn.value("prefab_key", "");
  envelope.target = spawn.value("network_id", "");
  envelope.ownershipGeneration = spawn.value("ownership_generation", 1ULL);
  envelope.data = spawn;
  return networkSessionSendEnvelope(host, session, std::move(envelope), peerId);
}

void networkSessionQueueSecureEvent(NetworkSessionState &session,
                                    const NetworkEnvelope &envelope,
                                    const std::string &trustedSender) {
  session.gameEvents.push_back({{"name", envelope.name},
                                {"sender_id", trustedSender},
                                {"target", envelope.target},
                                {"data", envelope.data}});
}

} // namespace

void LuaNetworkSessionBindingModule::install(LuaScriptHost &host,
                                             lua_State *state) const {
  sol::state_view lua(state);
  auto ownedSession = std::make_unique<NetworkSessionState>();
  NetworkSessionState *session = ownedSession.get();
  sol::table networkSession = lua.create_named_table("NetworkSession");
  networkSession["_state"] = std::move(ownedSession);
  networkSession.set_function("configure", [session](const sol::table options) {
    session->sendInterval =
        options.get_or("send_interval", session->sendInterval);
    session->extrapolationLimit =
        options.get_or("extrapolation_limit", session->extrapolationLimit);
    session->initialPrediction =
        options.get_or("initial_prediction", session->initialPrediction);
    session->channel = static_cast<std::uint8_t>(std::max(
        options.get_or("channel", static_cast<int>(session->channel)), 0));
    session->defaultPort = static_cast<std::uint16_t>(std::max(
        options.get_or("port", static_cast<int>(session->defaultPort)), 0));
    session->maxPeers = static_cast<std::uint32_t>(std::max(
        options.get_or("max_peers", static_cast<int>(session->maxPeers)), 1));
    session->certificate = options.get_or("certificate", session->certificate);
    session->privateKey = options.get_or("private_key", session->privateKey);
    session->trustedCertificate =
        options.get_or("trusted_certificate", session->trustedCertificate);
    session->serverName = options.get_or("server_name", session->serverName);
    const sol::object remotePrefab = options["remote_prefab"];
    if (remotePrefab.is<sol::table>()) {
      session->remotePrefab = remotePrefab.as<sol::table>();
    }
  });
  networkSession.set_function("sender_id", [&host, session] {
    return networkSessionSenderId(host, *session);
  });
  networkSession.set_function("is_host",
                              [&host] { return host.networkIsHost(); });
  networkSession.set_function("diagnostics", [state, &host, session] {
    return networkSessionDiagnostics(state, host, *session);
  });
  networkSession.set_function("owner", [state,
                                        session](const std::string &networkId) {
    if (const NetworkOwnedEntity *entity = session->ownership.find(networkId))
      return sol::make_object(state, entity->ownerPeerId);
    const auto owner = session->game.owner(networkId);
    return owner.has_value() ? sol::make_object(state, *owner) : sol::nil;
  });
  networkSession.set_function(
      "has_authority", [&host, session](const std::string &networkId) {
        if (const NetworkOwnedEntity *entity =
                session->ownership.find(networkId))
          return entity->ownerPeerId == networkSessionSenderId(host, *session);
        return session->game.hasAuthority(networkId);
      });
  networkSession.set_function("set_authority", [state, &host, session](
                                                   const std::string &networkId,
                                                   const std::string &owner) {
    if (host.networkContract() != nullptr) {
      session->game.reject(
          "set_authority is unavailable with a network contract; use transfer");
      return false;
    }
    if (!host.networkIsHost()) {
      session->game.reject("only the host may assign authority");
      return false;
    }
    if (!session->game.setOwner(networkId, owner))
      return false;
    sol::state_view lua(state);
    sol::table payload = lua.create_table();
    payload["network_id"] = networkId;
    payload["owner"] = owner;
    return networkSessionSendGameMessage(
        state, host, *session, "authority_changed",
        sol::make_object(state, payload), true);
  });
  networkSession.set_function("emit", [state, &host,
                                       session](const std::string &name,
                                                sol::optional<sol::object> data,
                                                sol::optional<bool> reliable) {
    if (host.networkContract() != nullptr) {
      session->game.reject(
          "NetworkSession.emit is unavailable with a network contract; use "
          "NetworkSession.send (Events.emit remains the local event bus)");
      return false;
    }
    if (name.empty()) {
      session->game.reject("game event name is required");
      return false;
    }
    sol::state_view lua(state);
    sol::table payload = lua.create_table();
    payload["name"] = name;
    payload["sender_id"] = networkSessionSenderId(host, *session);
    payload["data"] = data.value_or(sol::make_object(state, sol::nil));
    return networkSessionSendGameMessage(state, host, *session, "game_event",
                                         sol::make_object(state, payload),
                                         reliable.value_or(true));
  });
  networkSession.set_function("contract", [state, &host] {
    sol::state_view lua(state);
    sol::table result = lua.create_table();
    const NetworkContract *contract = host.networkContract();
    if (contract == nullptr) {
      result["active"] = false;
      return result;
    }
    result["active"] = true;
    result["id"] = contract->id;
    result["compatibility_hash"] = contract->compatibilityHash;
    result["maximum_message_bytes"] = contract->limits.maximumMessageBytes;
    result["maximum_owned_entities_per_peer"] =
        contract->limits.maximumOwnedEntitiesPerPeer;
    return result;
  });
  networkSession.set_function(
      "send", [&host, session](const std::string &name,
                               sol::optional<std::string> target,
                               sol::optional<sol::object> data) {
        const NetworkContract *contract = host.networkContract();
        if (contract == nullptr || !contract->messages.contains(name)) {
          session->game.reject("network message is not declared: " + name);
          return false;
        }
        NetworkEnvelope envelope;
        envelope.kind = NetworkEnvelopeKind::Message;
        envelope.name = name;
        envelope.target = target.value_or("");
        envelope.data = data.has_value() ? luaObjectToJson(*data)
                                         : nlohmann::json::object();
        if (const NetworkOwnedEntity *entity =
                session->ownership.find(envelope.target))
          envelope.ownershipGeneration = entity->ownershipGeneration;
        return networkSessionSendEnvelope(host, *session, std::move(envelope));
      });
  networkSession.set_function("spawn", [state, &host, session](
                                           const std::string &prefabKey,
                                           const std::string &entityId,
                                           sol::optional<std::string> owner) {
    const NetworkContract *contract = host.networkContract();
    if (contract == nullptr || !host.networkIsHost()) {
      session->game.reject("only the server may spawn declared entities");
      return sol::make_object(state, sol::nil);
    }
    if (!host.findEntityId(entityId).has_value()) {
      session->game.reject("cannot spawn missing local entity: " + entityId);
      return sol::make_object(state, sol::nil);
    }
    OwnershipResult spawned = session->ownership.spawn(
        *contract, prefabKey, owner.value_or("server"));
    if (!spawned.accepted) {
      session->game.reject(std::string(ownershipRejectCodeName(spawned.code)) +
                           ": " + spawned.reason);
      return sol::make_object(state, sol::nil);
    }
    const auto stateJson = host.captureEntityReplicatedState(
        entityId, *contract, prefabKey, NetworkActor::All);
    nlohmann::json payload = {
        {"network_id", spawned.entity->networkId},
        {"prefab_key", prefabKey},
        {"entity_id", entityId},
        {"owner", spawned.entity->ownerPeerId},
        {"session_epoch", spawned.entity->sessionEpoch},
        {"ownership_generation", spawned.entity->ownershipGeneration},
        {"state", stateJson ? nlohmann::json::parse(*stateJson)
                            : nlohmann::json::object()},
    };
    session->retainedSpawns[spawned.entity->networkId] = payload;
    session->localNetworkEntities[spawned.entity->networkId] = entityId;
    (void)session->game.registerEntity(spawned.entity->networkId,
                                       spawned.entity->ownerPeerId);
    if (!networkSessionSendRetainedSpawn(host, *session, payload))
      return sol::make_object(state, sol::nil);
    return sol::make_object(state, spawned.entity->networkId);
  });
  networkSession.set_function("transfer", [&host, session](
                                              const std::string &networkId,
                                              const std::string &newOwner) {
    const NetworkContract *contract = host.networkContract();
    if (contract == nullptr || !host.networkIsHost()) {
      session->game.reject("only the server may transfer ownership");
      return false;
    }
    OwnershipResult transfer =
        session->ownership.transfer(*contract, networkId, newOwner);
    if (!transfer.accepted) {
      session->game.reject(std::string(ownershipRejectCodeName(transfer.code)) +
                           ": " + transfer.reason);
      return false;
    }
    if (session->retainedSpawns.contains(networkId)) {
      session->retainedSpawns[networkId]["owner"] = newOwner;
      session->retainedSpawns[networkId]["ownership_generation"] =
          transfer.entity->ownershipGeneration;
    }
    NetworkEnvelope envelope{.kind = NetworkEnvelopeKind::Ownership,
                             .ownershipGeneration =
                                 transfer.entity->ownershipGeneration,
                             .name = "ownership",
                             .target = networkId,
                             .data = {{"owner", newOwner}}};
    return networkSessionSendEnvelope(host, *session, std::move(envelope));
  });
  networkSession.set_function(
      "register_entity",
      [state, &host, session](const std::string &entityId,
                              sol::optional<sol::table> options) {
        if (host.networkContract() != nullptr) {
          session->game.reject("register_entity is unavailable with a network "
                               "contract; use server spawn");
          return false;
        }
        if (!host.findEntityId(entityId).has_value()) {
          session->game.reject("cannot replicate missing entity: " + entityId);
          return false;
        }
        const sol::table config =
            options.value_or(sol::state_view(state).create_table());
        const std::string networkId = config.get_or("network_id", entityId);
        const std::string owner =
            config.get_or("owner", networkSessionSenderId(host, *session));
        if (!session->game.registerEntity(networkId, owner))
          return false;
        session->localNetworkEntities[networkId] = entityId;
        sol::table payload = networkSessionStatePayload(state, host, *session,
                                                        networkId, entityId);
        payload["owner"] = owner;
        return !host.networkAvailable() ||
               (!host.networkIsHost() && !host.networkIsConnected()) ||
               networkSessionSendGameMessage(
                   state, host, *session, "entity_spawn",
                   sol::make_object(state, payload), true);
      });
  networkSession.set_function("despawn", [state, &host, session](
                                             const std::string &networkId) {
    if (host.networkContract() != nullptr) {
      if (!host.networkIsHost()) {
        session->game.reject("only the server may despawn declared entities");
        return false;
      }
      OwnershipResult removed = session->ownership.despawn(networkId);
      if (!removed.accepted) {
        session->game.reject(
            std::string(ownershipRejectCodeName(removed.code)) + ": " +
            removed.reason);
        return false;
      }
      session->retainedSpawns.erase(networkId);
      session->localNetworkEntities.erase(networkId);
      (void)session->game.removeEntity(networkId);
      NetworkEnvelope envelope{.kind = NetworkEnvelopeKind::Despawn,
                               .ownershipGeneration =
                                   removed.entity->ownershipGeneration,
                               .name = "despawn",
                               .target = networkId,
                               .data = nlohmann::json::object()};
      return networkSessionSendEnvelope(host, *session, std::move(envelope));
    }
    if (!session->game.hasAuthority(networkId)) {
      session->game.reject("local peer has no authority to despawn " +
                           networkId);
      return false;
    }
    session->localNetworkEntities.erase(networkId);
    (void)session->game.removeEntity(networkId);
    sol::state_view lua(state);
    sol::table payload = lua.create_table();
    payload["network_id"] = networkId;
    payload["owner"] = networkSessionSenderId(host, *session);
    return networkSessionSendGameMessage(
        state, host, *session, "entity_despawn",
        sol::make_object(state, payload), true);
  });
  networkSession.set_function(
      "set_local_color",
      [session](float r, float g, float b, sol::optional<float> a) {
        session->localColor =
            Color{.r = r, .g = g, .b = b, .a = a.value_or(1.0F)};
      });
  networkSession.set_function("host", [&host,
                                       session](sol::optional<int> port) {
    if (!host.networkAvailable()) {
      return false;
    }
    networkSessionReset(host, *session, true);
    const std::uint16_t selectedPort = static_cast<std::uint16_t>(
        std::max(port.value_or(session->defaultPort), 0));
    bool hosted = false;
    if (!session->certificate.empty() || !session->privateKey.empty()) {
      hosted = !session->certificate.empty() && !session->privateKey.empty() &&
               host.networkHostSecure(selectedPort, session->certificate,
                                      session->privateKey, session->maxPeers);
    } else {
      hosted = host.networkHost(selectedPort, session->maxPeers);
    }
    if (hosted) {
      session->game.reset(true);
      session->ownership.reset(true);
      session->gateway.reset();
      session->outgoingSequence = 1;
      session->secureReady = true;
      (void)session->lifecycle.transition(NetworkSessionPhase::Connected);
      (void)session->lifecycle.transition(NetworkSessionPhase::Authenticated);
      (void)session->lifecycle.transition(NetworkSessionPhase::Ready);
      (void)session->lifecycle.transition(NetworkSessionPhase::Active);
    } else
      session->game.reject("failed to host network session");
    return hosted;
  });
  networkSession.set_function("connect", [&host, session](
                                             sol::optional<std::string> address,
                                             sol::optional<int> port) {
    if (!host.networkAvailable()) {
      return false;
    }
    networkSessionReset(host, *session, true);
    const std::string selectedAddress = address.value_or("127.0.0.1");
    const std::uint16_t selectedPort = static_cast<std::uint16_t>(
        std::max(port.value_or(session->defaultPort), 0));
    if (!session->trustedCertificate.empty()) {
      const bool connected = host.networkConnectSecure(
          selectedAddress, selectedPort, session->trustedCertificate,
          session->serverName.empty() ? selectedAddress : session->serverName);
      if (!connected)
        session->game.reject("failed to start secure connection");
      else
        session->game.reset(false);
      return connected;
    }
    const bool connected = host.networkConnect(selectedAddress, selectedPort);
    if (!connected)
      session->game.reject("failed to start connection");
    else
      session->game.reset(false);
    return connected;
  });
  networkSession.set_function("disconnect", [&host, session] {
    host.networkDisconnect();
    networkSessionReset(host, *session, true);
  });
  networkSession.set_function("is_connected",
                              [&host] { return host.networkIsConnected(); });
  networkSession.set_function("start_session", [state, &host, session](
                                                   const sol::object metadata) {
    session->sessionMetadata = metadata;
    if (metadata.valid() && metadata != sol::nil && host.networkAvailable() &&
        host.networkIsHost()) {
      if (host.networkContract() != nullptr) {
        NetworkEnvelope envelope{.kind = NetworkEnvelopeKind::Session,
                                 .name = "session_start",
                                 .target = "",
                                 .data = luaObjectToJson(metadata)};
        (void)networkSessionSendEnvelope(host, *session, std::move(envelope));
      } else {
        networkSessionSendMessage(state, host, "session_start", metadata, true,
                                  0, 0);
      }
    }
  });
  networkSession.set_function("current_session",
                              [session] { return session->sessionMetadata; });
  networkSession.set_function("reset_claims", [session] {
    session->claimObjects.clear();
    session->claimedObjects.clear();
  });
  networkSession.set_function(
      "remote_position", [state, session](const std::string &senderId) {
        const auto best = std::ranges::min_element(
            session->remotes, [&](const auto &left, const auto &right) {
              if (left.second.senderId != senderId) {
                return false;
              }
              if (right.second.senderId != senderId) {
                return true;
              }
              return left.second.age < right.second.age;
            });
        if (best == session->remotes.end() ||
            best->second.senderId != senderId) {
          return std::tuple<sol::object, sol::object>{sol::nil, sol::nil};
        }
        return std::tuple<sol::object, sol::object>{
            sol::make_object(state, best->second.x),
            sol::make_object(state, best->second.y)};
      });
  networkSession.set_function(
      "network_id_for_owner", [state, session](const std::string &owner) {
        for (const NetworkOwnedEntity &entity : session->ownership.snapshot())
          if (entity.ownerPeerId == owner)
            return sol::make_object(state, entity.networkId);
        return sol::make_object(state, sol::nil);
      });
  networkSession.set_function(
      "register_claim_once",
      [session](const std::string &id, sol::optional<sol::table> options) {
        if (id.empty()) {
          return false;
        }
        NetworkSessionClaimObject object;
        object.pending = false;
        if (options.has_value()) {
          const sol::table table = *options;
          const sol::object onRemoved = table["on_removed"];
          if (onRemoved.is<sol::function>()) {
            object.onRemoved = onRemoved.as<sol::function>();
          }
          const sol::object onClaimedLocal = table["on_claimed_local"];
          if (onClaimedLocal.is<sol::function>()) {
            object.onClaimedLocal = onClaimedLocal.as<sol::function>();
          }
          const sol::object canClaim = table["can_claim"];
          if (canClaim.is<sol::function>()) {
            object.canClaim = canClaim.as<sol::function>();
          }
        }
        const bool alreadyClaimed = session->claimedObjects.contains(id);
        session->claimObjects[id] = object;
        return !alreadyClaimed;
      });
  networkSession.set_function(
      "apply_claim_once",
      [state, &host, session](
          const std::string &id, const std::string &collectorId,
          sol::optional<bool> broadcast, sol::optional<sol::object> claim) {
        return networkSessionApplyClaimOnce(
            state, host, *session, id, collectorId, broadcast.value_or(false),
            claim.value_or(sol::make_object(state, sol::nil)));
      });
  networkSession.set_function(
      "request_claim_once_sync", [state, &host](sol::optional<int> peerId) {
        if (!host.networkAvailable()) {
          return false;
        }
        sol::state_view lua(state);
        return host.networkSend(
            encodeNetworkMessage("claim_once_sync_request",
                                 sol::make_object(state, lua.create_table())),
            true, 0,
            static_cast<std::uint32_t>(std::max(peerId.value_or(0), 0)));
      });
  networkSession.set_function(
      "try_claim_once",
      [state, &host, session](const std::string &id,
                              sol::optional<sol::object> claim) {
        auto object = session->claimObjects.find(id);
        if (object == session->claimObjects.end() || object->second.pending ||
            session->claimedObjects.contains(id)) {
          return false;
        }
        const sol::object claimObject =
            claim.value_or(sol::make_object(state, sol::nil));
        if (!host.networkAvailable() || host.networkIsHost() ||
            !host.networkIsConnected()) {
          return networkSessionApplyClaimOnce(
              state, host, *session, id, networkSessionSenderId(host, *session),
              true, claimObject);
        }
        object->second.pending = true;
        sol::state_view lua(state);
        sol::table payload = claimObject.is<sol::table>()
                                 ? claimObject.as<sol::table>()
                                 : lua.create_table();
        payload["object_id"] = id;
        return networkSessionSendMessage(state, host, "claim_once_request",
                                         sol::make_object(state, payload), true,
                                         0, 0);
      });
  networkSession.set_function("process_events", [state, &host, session] {
    sol::state_view lua(state);
    sol::table summary = lua.create_table();
    summary["connected"] = false;
    summary["disconnected"] = false;
    summary["session_started"] = false;
    summary["session"] = sol::nil;
    summary["messages"] = 0;
    summary["events"] = lua.create_table();
    if (!host.networkAvailable()) {
      return summary;
    }
    int messages = 0;
    for (const NetworkEvent &event : host.networkDrainEvents()) {
      if (event.type == NetworkEventType::Connected) {
        summary["connected"] = true;
        session->game.peerConnected(event.peerId);
        if (!host.networkIsHost())
          (void)session->lifecycle.transition(NetworkSessionPhase::Connected);
        if (host.networkIsHost()) {
          const std::string assignedPeer =
              "peer" + std::to_string(event.peerId);
          if (host.networkContract() == nullptr) {
            sol::table assign = lua.create_table();
            assign["peer_id"] = assignedPeer;
            networkSessionSendMessage(state, host, "assign_peer",
                                      sol::make_object(state, assign), true,
                                      event.peerId, 0);
            if (session->sessionMetadata.valid() &&
                session->sessionMetadata != sol::nil) {
              networkSessionSendMessage(state, host, "session_start",
                                        session->sessionMetadata, true,
                                        event.peerId, 0);
            }
          } else if (const NetworkContract *contract = host.networkContract()) {
            NetworkEnvelope handshake{
                .kind = NetworkEnvelopeKind::Session,
                .name = "secure_session",
                .target = "",
                .data = {{"peer_id", assignedPeer},
                         {"contract_hash", contract->compatibilityHash}}};
            (void)networkSessionSendEnvelope(
                host, *session, std::move(handshake), event.peerId);
            if (session->sessionMetadata.valid() &&
                session->sessionMetadata != sol::nil) {
              NetworkEnvelope started{
                  .kind = NetworkEnvelopeKind::Session,
                  .name = "session_start",
                  .target = "",
                  .data = luaObjectToJson(session->sessionMetadata)};
              (void)networkSessionSendEnvelope(
                  host, *session, std::move(started), event.peerId);
            }
            for (const auto &[unused, spawn] : session->retainedSpawns) {
              (void)unused;
              (void)networkSessionSendRetainedSpawn(host, *session, spawn,
                                                    event.peerId);
            }
          }
          networkSessionSendClaimSync(state, host, *session, event.peerId);
        }
      } else if (event.type == NetworkEventType::Disconnected) {
        summary["disconnected"] = true;
        if (host.networkIsHost() && host.networkContract() != nullptr) {
          const std::string peer = session->game.peerName(event.peerId);
          const DisconnectOwnershipActions actions =
              session->ownership.disconnectPeer(*host.networkContract(), peer);
          for (const NetworkOwnedEntity &entity : actions.despawned) {
            session->retainedSpawns.erase(entity.networkId);
            NetworkEnvelope envelope{.kind = NetworkEnvelopeKind::Despawn,
                                     .ownershipGeneration =
                                         entity.ownershipGeneration,
                                     .name = "despawn",
                                     .target = entity.networkId,
                                     .data = nlohmann::json::object()};
            (void)networkSessionSendEnvelope(host, *session,
                                             std::move(envelope));
          }
          const auto broadcastOwner = [&](const NetworkOwnedEntity &entity) {
            if (session->retainedSpawns.contains(entity.networkId)) {
              session->retainedSpawns[entity.networkId]["owner"] = "server";
              session
                  ->retainedSpawns[entity.networkId]["ownership_generation"] =
                  entity.ownershipGeneration;
            }
            NetworkEnvelope envelope{.kind = NetworkEnvelopeKind::Ownership,
                                     .ownershipGeneration =
                                         entity.ownershipGeneration,
                                     .name = "ownership",
                                     .target = entity.networkId,
                                     .data = {{"owner", "server"}}};
            (void)networkSessionSendEnvelope(host, *session,
                                             std::move(envelope));
          };
          for (const auto &entity : actions.returnedToServer)
            broadcastOwner(entity);
          for (const auto &entity : actions.awaitingGamePolicy)
            broadcastOwner(entity);
        }
        session->game.peerDisconnected(event.peerId);
        if (host.networkIsHost()) {
          const std::string senderId = "peer" + std::to_string(event.peerId);
          for (auto it = session->remotes.begin();
               it != session->remotes.end();) {
            if (it->second.senderId == senderId) {
              (void)host.destroyEntity(it->first);
              it = session->remotes.erase(it);
            } else {
              ++it;
            }
          }
        } else {
          networkSessionReset(host, *session, true);
        }
      } else if (event.type == NetworkEventType::Message) {
        ++messages;
        session->game.messageReceived();
        if (event.message.size() >= 4 &&
            event.message.compare(0, 4, "DNET") == 0) {
          const auto *bytes =
              reinterpret_cast<const std::uint8_t *>(event.message.data());
          const std::string trustedSender =
              host.networkIsHost() ? session->game.peerName(event.peerId)
                                   : "server";
          const NetworkGatewayResult accepted = session->gateway.accept(
              std::span<const std::uint8_t>(bytes, event.message.size()),
              {.authoritativeServer = host.networkIsHost(),
               .trustedSenderPeerId = trustedSender,
               .localPeerId = networkSessionSenderId(host, *session),
               .nowSeconds = host.gameTime(),
               .contract = host.networkContract(),
               .ownership = &session->ownership});
          if (!accepted.accepted) {
            session->game.reject(
                std::string(networkGatewayRejectCodeName(accepted.code)));
            continue;
          }
          const NetworkEnvelope &envelope = *accepted.envelope;
          if (envelope.kind == NetworkEnvelopeKind::Session &&
              !host.networkIsHost() && envelope.name == "secure_session") {
            const std::string peer =
                envelope.data.value("peer_id", std::string{});
            if (peer.empty() ||
                !session->ownership.synchronizeEpoch(envelope.sessionEpoch)) {
              session->game.reject("secure session epoch is invalid");
              host.networkDisconnect();
              continue;
            }
            session->localPeerId = peer;
            session->game.setLocalPeerId(peer);
            session->gateway.reset();
            session->secureReady = true;
            (void)session->lifecycle.transition(
                NetworkSessionPhase::Authenticated);
            (void)session->lifecycle.transition(NetworkSessionPhase::Ready);
            (void)session->lifecycle.transition(NetworkSessionPhase::Active);
          } else if (envelope.kind == NetworkEnvelopeKind::Session &&
                     !host.networkIsHost() &&
                     envelope.name == "session_start") {
            sol::object payloadObject = jsonToLuaObject(state, envelope.data);
            session->sessionMetadata = payloadObject;
            summary["session_started"] = true;
            summary["session"] = payloadObject;
          } else if (envelope.kind == NetworkEnvelopeKind::Message) {
            if (envelope.name == "state_update") {
              const NetworkOwnedEntity *entity =
                  session->ownership.find(envelope.target);
              const nlohmann::json stateJson =
                  envelope.data.value("state", nlohmann::json::object());
              const NetworkActor writer =
                  entity != nullptr && entity->ownerPeerId == "server"
                      ? NetworkActor::Server
                      : NetworkActor::Owner;
              if (entity == nullptr ||
                  !validateContractReplicatedState(*host.networkContract(),
                                                   entity->prefabKey, writer,
                                                   stateJson)
                       .ok) {
                session->game.reject("state update violates field policy");
                continue;
              }
              nlohmann::json sanitized = envelope.data;
              sanitized["network_id"] = envelope.target;
              sanitized["owner"] = entity->ownerPeerId;
              sol::object payloadObject = jsonToLuaObject(state, sanitized);
              if (!payloadObject.is<sol::table>() ||
                  !networkSessionCreateOrApplyRemote(
                      state, host, *session, payloadObject.as<sol::table>()))
                continue;
              if (host.networkIsHost() &&
                  session->retainedSpawns.contains(envelope.target))
                session->retainedSpawns[envelope.target]["state"] = stateJson;
              if (host.networkIsHost()) {
                NetworkEnvelope relayed = envelope;
                relayed.data = std::move(sanitized);
                (void)networkSessionSendEnvelope(host, *session,
                                                 std::move(relayed));
              }
              continue;
            }
            std::string eventSender = trustedSender;
            if (!host.networkIsHost() && envelope.data.is_object())
              eventSender = envelope.data.value("_sender_id", "server");
            networkSessionQueueSecureEvent(*session, envelope, eventSender);
            if (host.networkIsHost()) {
              const NetworkMessageRule &rule =
                  host.networkContract()->messages.at(envelope.name);
              if (rule.to == NetworkActor::All) {
                NetworkEnvelope relayed = envelope;
                if (!relayed.data.is_object())
                  relayed.data = {{"value", relayed.data}};
                relayed.data["_sender_id"] = trustedSender;
                (void)networkSessionSendEnvelope(host, *session,
                                                 std::move(relayed));
              }
            }
          } else if (envelope.kind == NetworkEnvelopeKind::Spawn) {
            NetworkOwnedEntity entity{
                .networkId = envelope.target,
                .prefabKey = envelope.name,
                .ownerPeerId = envelope.data.value("owner", "server"),
                .sessionEpoch = envelope.sessionEpoch,
                .ownershipGeneration = envelope.ownershipGeneration};
            const OwnershipResult applied =
                session->ownership.applyAuthoritativeSpawn(entity);
            if (!applied.accepted)
              continue;
            (void)session->game.registerEntity(entity.networkId,
                                               entity.ownerPeerId);
            if (entity.ownerPeerId == networkSessionSenderId(host, *session))
              session->localNetworkEntities[entity.networkId] =
                  envelope.data.value("entity_id", std::string{});
            sol::object payloadObject = jsonToLuaObject(state, envelope.data);
            if (payloadObject.is<sol::table>())
              (void)networkSessionCreateOrApplyRemote(
                  state, host, *session, payloadObject.as<sol::table>());
          } else if (envelope.kind == NetworkEnvelopeKind::Ownership) {
            (void)session->ownership.applyAuthoritativeTransfer(
                envelope.target, envelope.data.value("owner", "server"),
                envelope.sessionEpoch, envelope.ownershipGeneration);
          } else if (envelope.kind == NetworkEnvelopeKind::Despawn) {
            const NetworkOwnedEntity *owned =
                session->ownership.find(envelope.target);
            const std::string owner =
                owned == nullptr ? std::string{} : owned->ownerPeerId;
            const OwnershipResult applied =
                session->ownership.applyAuthoritativeDespawn(
                    envelope.target, envelope.sessionEpoch,
                    envelope.ownershipGeneration);
            if (applied.accepted && !owner.empty())
              (void)host.destroyEntity(
                  networkSessionRemoteId(owner, envelope.target));
          }
          continue;
        }
        const sol::object decoded = decodeNetworkMessage(state, event.message);
        if (!decoded.is<sol::table>()) {
          continue;
        }
        const sol::table message = decoded.as<sol::table>();
        const std::string type = message.get_or("type", std::string{});
        const sol::object payloadObject = message["payload"];
        sol::table payload = payloadObject.is<sol::table>()
                                 ? payloadObject.as<sol::table>()
                                 : lua.create_table();
        if (host.networkContract() != nullptr) {
          session->game.reject("legacy network message rejected by contract");
          continue;
        }
        if (type == "assign_peer") {
          session->localPeerId = payload.get_or("peer_id", std::string{});
          session->game.setLocalPeerId(session->localPeerId);
        } else if (type == "session_start") {
          session->sessionMetadata = payloadObject;
          summary["session_started"] = true;
          summary["session"] = payloadObject;
        } else if (type == "transform_snapshot") {
          if (host.networkIsHost() &&
              payload.get_or("sender_id", std::string{}) != "host") {
            payload["sender_id"] = "peer" + std::to_string(event.peerId);
            networkSessionSendMessage(state, host, "transform_snapshot",
                                      sol::make_object(state, payload), false,
                                      0, session->channel);
          }
          networkSessionApplySnapshot(host, *session, payload);
        } else if (type == "claim_once_request" && host.networkIsHost()) {
          const std::string objectId =
              payload.get_or("object_id", std::string{});
          const std::string collectorId = "peer" + std::to_string(event.peerId);
          if (const auto claimed = session->claimedObjects.find(objectId);
              claimed != session->claimedObjects.end()) {
            sol::table claimedPayload = lua.create_table();
            claimedPayload["object_id"] = objectId;
            claimedPayload["collector_id"] = claimed->second;
            networkSessionSendMessage(state, host, "claim_once_claimed",
                                      sol::make_object(state, claimedPayload),
                                      true, event.peerId, 0);
          } else if (!networkSessionApplyClaimOnce(state, host, *session,
                                                   objectId, collectorId, true,
                                                   payloadObject)) {
            sol::table rejectedPayload = lua.create_table();
            rejectedPayload["object_id"] = objectId;
            rejectedPayload["collector_id"] = collectorId;
            networkSessionSendMessage(state, host, "claim_once_rejected",
                                      sol::make_object(state, rejectedPayload),
                                      true, event.peerId, 0);
          }
        } else if (type == "claim_once_claimed") {
          networkSessionApplyClaimOnce(
              state, host, *session, payload.get_or("object_id", std::string{}),
              payload.get_or("collector_id", std::string{}), false,
              payloadObject);
        } else if (type == "claim_once_rejected") {
          if (auto object = session->claimObjects.find(
                  payload.get_or("object_id", std::string{}));
              object != session->claimObjects.end()) {
            object->second.pending = false;
          }
        } else if (type == "claim_once_sync") {
          const sol::object claimsObject = payload["claims"];
          if (claimsObject.is<sol::table>()) {
            const sol::table claims = claimsObject.as<sol::table>();
            for (const auto &[_, claimObject] : claims) {
              if (claimObject.is<sol::table>()) {
                const sol::table claim = claimObject.as<sol::table>();
                networkSessionApplyClaimOnce(
                    state, host, *session,
                    claim.get_or("object_id", std::string{}),
                    claim.get_or("collector_id", std::string{}), false,
                    claimObject);
              }
            }
          }
        } else if (type == "claim_once_sync_request" && host.networkIsHost()) {
          networkSessionSendClaimSync(state, host, *session, event.peerId);
        } else if (type == "game_event") {
          if (host.networkIsHost()) {
            payload["sender_id"] = session->game.peerName(event.peerId);
            networkSessionSendGameMessage(state, host, *session, "game_event",
                                          sol::make_object(state, payload),
                                          true);
          }
          session->gameEvents.push_back(luaObjectToJson(payloadObject));
        } else if (type == "entity_spawn") {
          std::string owner = payload.get_or("owner", std::string{});
          if (host.networkIsHost()) {
            owner = session->game.peerName(event.peerId);
            payload["owner"] = owner;
          }
          const std::string networkId =
              payload.get_or("network_id", std::string{});
          if (!session->game.registerEntity(networkId, owner)) {
            continue;
          }
          if (!networkSessionCreateOrApplyRemote(state, host, *session,
                                                 payload))
            continue;
          if (host.networkIsHost()) {
            networkSessionSendGameMessage(state, host, *session, "entity_spawn",
                                          sol::make_object(state, payload),
                                          true);
          }
        } else if (type == "state_snapshot") {
          std::string owner = payload.get_or("owner", std::string{});
          const std::string networkId =
              payload.get_or("network_id", std::string{});
          if (host.networkIsHost()) {
            owner = session->game.peerName(event.peerId);
            payload["owner"] = owner;
          }
          if (!session->game.acceptsStateFrom(networkId, owner)) {
            session->game.reject("rejected state without authority for " +
                                 networkId);
            continue;
          }
          if (!networkSessionCreateOrApplyRemote(state, host, *session,
                                                 payload))
            continue;
          if (host.networkIsHost()) {
            networkSessionSendGameMessage(
                state, host, *session, "state_snapshot",
                sol::make_object(state, payload), false, 0, session->channel);
          }
        } else if (type == "entity_despawn") {
          const std::string networkId =
              payload.get_or("network_id", std::string{});
          std::string owner = payload.get_or("owner", std::string{});
          if (host.networkIsHost())
            owner = session->game.peerName(event.peerId);
          if (!session->game.acceptsStateFrom(networkId, owner)) {
            session->game.reject("rejected despawn without authority for " +
                                 networkId);
            continue;
          }
          (void)host.destroyEntity(networkSessionRemoteId(owner, networkId));
          (void)session->game.removeEntity(networkId);
          if (host.networkIsHost()) {
            payload["owner"] = owner;
            networkSessionSendGameMessage(
                state, host, *session, "entity_despawn",
                sol::make_object(state, payload), true);
          }
        } else if (type == "authority_changed" && !host.networkIsHost()) {
          (void)session->game.setOwner(
              payload.get_or("network_id", std::string{}),
              payload.get_or("owner", std::string{}));
        }
      }
    }
    summary["messages"] = messages;
    sol::table gameEvents = lua.create_table();
    int eventIndex = 1;
    for (const nlohmann::json &event : session->gameEvents)
      gameEvents[eventIndex++] = jsonToLuaObject(state, event);
    session->gameEvents.clear();
    summary["events"] = gameEvents;
    return summary;
  });
  networkSession.set_function("update_entity", [state, &host, session](
                                                   const std::string &networkId,
                                                   const float dt) {
    if (!host.networkAvailable())
      return true;
    for (auto &[ghostId, remote] : session->remotes) {
      remote.age = std::min(remote.age + dt, session->extrapolationLimit);
      (void)host.setEntityPosition(ghostId, remote.x + remote.vx * remote.age,
                                   remote.y + remote.vy * remote.age);
    }
    if (const NetworkContract *contract = host.networkContract()) {
      const NetworkOwnedEntity *owned = session->ownership.find(networkId);
      const auto local = session->localNetworkEntities.find(networkId);
      if (owned == nullptr || local == session->localNetworkEntities.end() ||
          owned->ownerPeerId != networkSessionSenderId(host, *session))
        return false;
      session->accumulator += dt;
      if (session->accumulator < session->sendInterval)
        return true;
      session->accumulator = 0.0F;
      const NetworkActor writer =
          host.networkIsHost() ? NetworkActor::Server : NetworkActor::Owner;
      const auto stateJson = host.captureEntityReplicatedState(
          local->second, *contract, owned->prefabKey, writer);
      if (!stateJson)
        return false;
      NetworkEnvelope envelope{
          .kind = NetworkEnvelopeKind::Message,
          .ownershipGeneration = owned->ownershipGeneration,
          .name = "state_update",
          .target = networkId,
          .data = {{"network_id", networkId},
                   {"owner", owned->ownerPeerId},
                   {"state", nlohmann::json::parse(*stateJson)}}};
      return networkSessionSendEnvelope(host, *session, std::move(envelope));
    }
    const auto local = session->localNetworkEntities.find(networkId);
    if (local == session->localNetworkEntities.end() ||
        !session->game.hasAuthority(networkId))
      return false;
    session->accumulator += dt;
    if (session->accumulator < session->sendInterval)
      return true;
    session->accumulator = 0.0F;
    sol::table payload = networkSessionStatePayload(state, host, *session,
                                                    networkId, local->second);
    return networkSessionSendGameMessage(
        state, host, *session, "state_snapshot",
        sol::make_object(state, payload), false, 0, session->channel);
  });
}

} // namespace demi::runtime
