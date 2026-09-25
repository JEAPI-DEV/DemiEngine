#include "demi/runtime/scripting/bindings/LuaNetworkSessionBindings.h"
#include "demi/runtime/network/GameNetworkSession.h"
#include "demi/runtime/network/NetworkMessageGateway.h"
#include "demi/runtime/network/NetworkOwnershipRegistry.h"
#include "demi/runtime/network/NetworkPrediction.h"
#include "demi/runtime/network/NetworkQueryHistory2D.h"
#include "demi/runtime/network/NetworkSessionProtocol.h"
#include "demi/runtime/network/ReplicatedState.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"
#include "demi/runtime/scripting/bindings/LuaNetworkQueryHistoryBindings.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

namespace {

struct NetworkSessionRemote {
  std::string senderId;
  NetworkRemoteMotion2D motion;
};

struct NetworkSessionState {
  float sendInterval = 1.0F / 60.0F;
  float extrapolationLimit = 0.10F;
  float initialPrediction = 0.025F;
  std::uint16_t defaultPort = 39420;
  std::uint32_t maxPeers = 8;
  std::string certificate;
  std::string privateKey;
  std::string trustedCertificate;
  std::string serverName;
  GameNetworkSession game;
  NetworkSessionProtocol protocol{game};
  sol::object sessionMetadata = sol::nil;
  sol::table remotePrefab;
  std::unordered_map<std::string, NetworkSessionRemote> remotes;
  std::unordered_map<std::string, std::string> localNetworkEntities;
  std::vector<nlohmann::json> gameEvents;
  NetworkQueryHistory2D queryHistory{{}};
};

std::string networkSessionSenderId(const NetworkSessionState &session) {
  const auto &peer = session.protocol.localPeerId();
  return peer.empty() ? "client" : peer;
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
                                 sol::table snapshot,
                                 bool alreadySampled = false);
void networkSessionReset(LuaScriptHost &host, NetworkSessionState &session,
                         const bool clearRemoteEntities) {
  if (clearRemoteEntities) {
    for (const auto &[ghostId, _] : session.remotes) {
      (void)host.destroyEntity(ghostId);
    }
  }
  session.game.reset(host.networkIsHost());
  session.protocol.reset(host.networkIsHost());
  session.remotes.clear();
  session.localNetworkEntities.clear();
  session.gameEvents.clear();
  session.queryHistory.clear();
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
  result["local_peer_id"] = networkSessionSenderId(session);
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
  result["session_epoch"] = session.protocol.ownership().sessionEpoch();
  result["contract_hash"] = host.networkContract() == nullptr
                                ? std::string{}
                                : host.networkContract()->compatibilityHash;
  result["secure_accepted_messages"] = session.protocol.counters().accepted;
  std::uint64_t secureRejected = 0;
  for (const auto &[unused, count] : session.protocol.counters().rejected) {
    (void)unused;
    secureRejected += count;
  }
  result["secure_rejected_messages"] = secureRejected;
  result["secure_ready"] = session.protocol.ready();
  result["phase"] =
      std::string(networkSessionPhaseName(session.protocol.phase()));
  return result;
}

bool networkSessionCreateOrApplyRemote(lua_State *state, LuaScriptHost &host,
                                       NetworkSessionState &session,
                                       const sol::table payload) {
  const std::string owner = payload.get_or("owner", std::string{});
  const std::string networkId = payload.get_or("network_id", std::string{});
  const sol::object stateObject = payload["state"];
  if (owner.empty() || networkId.empty() || !stateObject.is<sol::table>())
    return false;
  if (owner == networkSessionSenderId(session))
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

void networkSessionApplySnapshot(LuaScriptHost &host,
                                 NetworkSessionState &session,
                                 const sol::table snapshot,
                                 bool alreadySampled) {
  const std::string senderId = snapshot.get_or("sender_id", std::string{});
  if (senderId.empty() || senderId == networkSessionSenderId(session) ||
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
  remote.motion.x = snapshot.get_or("x", 0.0F);
  remote.motion.y = snapshot.get_or("y", 0.0F);
  remote.motion.vx = snapshot.get_or("vx", 0.0F);
  remote.motion.vy = snapshot.get_or("vy", 0.0F);
  remote.motion.receivedAtSeconds = host.gameTime();
  remote.motion.alreadySampled = alreadySampled;
  if (snapshot["color"].is<sol::table>()) {
    (void)host.setEntitySpriteColor(
        ghostId, luaColorField(snapshot, "color",
                               luaColorField(session.remotePrefab, "color")));
  }
  const auto [x, y] = remote.motion.position(
      host.gameTime(), session.extrapolationLimit, session.initialPrediction);
  (void)host.setEntityPosition(ghostId, x, y);
}

bool networkSessionSendEnvelope(LuaScriptHost &host,
                                NetworkSessionState &session,
                                NetworkEnvelope envelope,
                                const std::uint32_t peerId = 0) {
  // Session sequences share one ordered transport channel.
  return session.protocol.send(
      host.networkContract(), std::move(envelope),
      [&host](const std::string &wire, bool reliable, std::uint32_t peer) {
        return host.networkSend(wire, reliable, 0, peer);
      },
      peerId);
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
    NetworkSessionPrediction::Config prediction;
    session->sendInterval =
        options.get_or("send_interval", session->sendInterval);
    session->extrapolationLimit =
        options.get_or("extrapolation_limit", session->extrapolationLimit);
    session->initialPrediction =
        options.get_or("initial_prediction", session->initialPrediction);
    prediction.interpolation.interpolationDelaySeconds =
        options.get_or("interpolation_delay", 0.0);
    prediction.interpolation.extrapolationLimitSeconds =
        static_cast<double>(session->extrapolationLimit);
    prediction.interpolation.capacity = static_cast<std::size_t>(
        std::max(options.get_or("snapshot_buffer", 0), 0));
    prediction.inputQueue.capacity = static_cast<std::size_t>(
        std::max(options.get_or("input_queue_capacity", 0), 0));
    prediction.inputQueue.futureWindow = static_cast<std::uint64_t>(
        std::max(options.get_or("input_future_window", 0), 0));
    prediction.inputQueue.headOfLineTimeoutSeconds =
        options.get_or("input_head_of_line_timeout", 0.0);
    prediction.maximumInputsPerTick = static_cast<std::size_t>(
        std::max(options.get_or("input_max_per_tick", 0), 0));
    prediction.controller.inputHistoryLimit = static_cast<std::size_t>(
        std::max(options.get_or("prediction_history_limit", 0), 0));
    prediction.controller.visualOffsetDecayPerSecond =
        options.get_or("prediction_visual_decay", 0.0);
    session->queryHistory = NetworkQueryHistory2D({
        .snapshotCapacity = static_cast<std::size_t>(
            std::max(options.get_or("query_history_capacity", 0), 0)),
        .maximumCirclesPerSnapshot = static_cast<std::size_t>(
            std::max(options.get_or("query_history_max_entities", 0), 0)),
        .maximumRewindTicks = static_cast<std::uint64_t>(
            std::max(options.get_or("query_history_rewind_ticks", 0), 0)),
    });
    session->protocol.prediction().configure(std::move(prediction));
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
  installNetworkQueryHistoryBindings(host, state, session->game,
                                     session->queryHistory);
  networkSession.set_function(
      "sender_id", [session] { return networkSessionSenderId(*session); });
  networkSession.set_function("is_host",
                              [&host] { return host.networkIsHost(); });
  networkSession.set_function("diagnostics", [state, &host, session] {
    return networkSessionDiagnostics(state, host, *session);
  });
  networkSession.set_function(
      "owner", [state, session](const std::string &networkId) {
        if (const NetworkOwnedEntity *entity =
                session->protocol.ownership().find(networkId))
          return sol::make_object(state, entity->ownerPeerId);
        return sol::make_object(state, sol::nil);
      });
  networkSession.set_function(
      "has_authority", [session](const std::string &networkId) {
        if (const NetworkOwnedEntity *entity =
                session->protocol.ownership().find(networkId))
          return entity->ownerPeerId == networkSessionSenderId(*session);
        return false;
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
  networkSession.set_function("send", [&host, session](
                                          const std::string &name,
                                          sol::optional<std::string> target,
                                          sol::optional<sol::object> data) {
    auto envelope = session->protocol.message(
        host.networkContract(), name, target.value_or(""),
        data.has_value() ? luaObjectToJson(*data) : nlohmann::json::object());
    return envelope &&
           networkSessionSendEnvelope(host, *session, std::move(*envelope));
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
    const auto stateJson = host.captureEntityReplicatedState(
        entityId, *contract, prefabKey, NetworkActor::All);
    if (!stateJson) {
      session->game.reject("failed to capture declared spawn state");
      return sol::make_object(state, sol::nil);
    }
    auto spawned = session->protocol.spawn(contract, prefabKey, entityId,
                                           owner.value_or("server"),
                                           nlohmann::json::parse(*stateJson));
    if (!spawned)
      return sol::make_object(state, sol::nil);
    const std::string networkId = spawned->target;
    session->localNetworkEntities[networkId] = entityId;
    if (!networkSessionSendEnvelope(host, *session, std::move(*spawned)))
      return sol::make_object(state, sol::nil);
    return sol::make_object(state, networkId);
  });
  networkSession.set_function(
      "transfer", [&host, session](const std::string &networkId,
                                   const std::string &newOwner) {
        auto transfer = session->protocol.transfer(host.networkContract(),
                                                   networkId, newOwner);
        if (!transfer)
          return false;
        return networkSessionSendEnvelope(host, *session, std::move(*transfer));
      });
  networkSession.set_function(
      "bind_local_entity", [&host, session](const std::string &networkId,
                                            const std::string &entityId) {
        const NetworkOwnedEntity *owned =
            session->protocol.ownership().find(networkId);
        if (owned == nullptr ||
            owned->ownerPeerId != networkSessionSenderId(*session)) {
          session->game.reject(
              "only the owning peer may bind a local network entity");
          return false;
        }
        if (!host.findEntityId(entityId).has_value()) {
          session->game.reject("cannot bind missing local entity: " + entityId);
          return false;
        }
        session->localNetworkEntities[networkId] = entityId;
        return true;
      });
  networkSession.set_function("despawn", [&host, session](
                                             const std::string &networkId) {
    auto removed = session->protocol.despawn(host.networkContract(), networkId);
    if (!removed)
      return false;
    session->localNetworkEntities.erase(networkId);
    return networkSessionSendEnvelope(host, *session, std::move(*removed));
  });
  networkSession.set_function("host", [&host,
                                       session](sol::optional<int> port) {
    if (!host.networkAvailable() || host.networkContract() == nullptr) {
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
      session->protocol.activateHost();
    } else
      session->game.reject("failed to host network session");
    return hosted;
  });
  networkSession.set_function("connect", [&host, session](
                                             sol::optional<std::string> address,
                                             sol::optional<int> port) {
    if (!host.networkAvailable() || host.networkContract() == nullptr) {
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
      else {
        session->game.reset(false);
        session->protocol.reset(false);
      }
      return connected;
    }
    const bool connected = host.networkConnect(selectedAddress, selectedPort);
    if (!connected)
      session->game.reject("failed to start connection");
    else {
      session->game.reset(false);
      session->protocol.reset(false);
    }
    return connected;
  });
  networkSession.set_function("disconnect", [&host, session] {
    host.networkDisconnect();
    networkSessionReset(host, *session, true);
  });
  networkSession.set_function("is_connected",
                              [&host] { return host.networkIsConnected(); });
  networkSession.set_function(
      "start_session", [state, &host, session](const sol::object metadata) {
        session->sessionMetadata = metadata;
        if (metadata.valid() && metadata != sol::nil &&
            host.networkAvailable() && host.networkIsHost()) {
          NetworkEnvelope envelope{.kind = NetworkEnvelopeKind::Session,
                                   .name = "session_start",
                                   .target = "",
                                   .data = luaObjectToJson(metadata)};
          (void)networkSessionSendEnvelope(host, *session, std::move(envelope));
        }
      });
  networkSession.set_function("current_session",
                              [session] { return session->sessionMetadata; });
  networkSession.set_function(
      "remote_position", [state, &host, session](const std::string &senderId) {
        const auto best = std::ranges::min_element(
            session->remotes, [&](const auto &left, const auto &right) {
              if (left.second.senderId != senderId) {
                return false;
              }
              if (right.second.senderId != senderId) {
                return true;
              }
              return left.second.motion.receivedAtSeconds >
                     right.second.motion.receivedAtSeconds;
            });
        if (best == session->remotes.end() ||
            best->second.senderId != senderId) {
          return std::tuple<sol::object, sol::object>{sol::nil, sol::nil};
        }
        const auto [x, y] = best->second.motion.position(
            host.gameTime(), session->extrapolationLimit,
            session->initialPrediction);
        return std::tuple<sol::object, sol::object>{sol::make_object(state, x),
                                                    sol::make_object(state, y)};
      });
  networkSession.set_function(
      "network_id_for_owner", [state, session](const std::string &owner) {
        for (const NetworkOwnedEntity &entity :
             session->protocol.ownership().snapshot())
          if (entity.ownerPeerId == owner)
            return sol::make_object(state, entity.networkId);
        return sol::make_object(state, sol::nil);
      });
  networkSession.set_function("take_inputs", [state, &host,
                                              session](const std::string &id) {
    return jsonToLuaObject(
        state, session->protocol.prediction().takeInputs(id, host.gameTime()));
  });
  networkSession.set_function(
      "publish_snapshot",
      [&host, session](const std::string &id, sol::object value,
                       sol::optional<sol::table> options) {
        auto envelope = session->protocol.prediction().publish(
            host.networkContract(), id, luaObjectToJson(value),
            options ? options->get_or("marker", std::string{"normal"})
                    : "normal");
        return envelope &&
               networkSessionSendEnvelope(host, *session, std::move(*envelope));
      });
  networkSession.set_function("enable_prediction", [state, &host, session](
                                                       sol::table options) {
    const sol::object callback = options["apply"];
    if (!callback.is<sol::function>()) {
      session->game.reject(
          "prediction requires an apply(state, input) callback");
      return false;
    }
    const sol::protected_function apply =
        callback.as<sol::protected_function>();
    return session->protocol.prediction().enable(
        host.networkContract(), options.get_or("network_id", std::string{}),
        options.get_or("input_message", std::string{}),
        luaObjectToJson(options["state"]),
        [state, apply](const nlohmann::json &value, const nlohmann::json &input)
            -> std::optional<nlohmann::json> {
          const sol::protected_function_result result = apply(
              jsonToLuaObject(state, value), jsonToLuaObject(state, input));
          if (!result.valid() || !result.get<sol::object>().is<sol::table>())
            return std::nullopt;
          return luaObjectToJson(result.get<sol::object>());
        },
        host.gameTime());
  });
  networkSession.set_function(
      "disable_prediction", [session](const std::string &id) {
        return session->protocol.prediction().disable(id);
      });
  networkSession.set_function(
      "reset_prediction", [session](const std::string &id, sol::object value) {
        return session->protocol.prediction().rebase(id,
                                                     luaObjectToJson(value));
      });
  networkSession.set_function(
      "predict_input",
      [state, &host, session](const std::string &id, sol::object input) {
        auto envelope =
            session->protocol.prediction().predict(id, luaObjectToJson(input));
        if (!envelope)
          return sol::make_object(state, sol::nil);
        const auto sequence = envelope->data.at("seq").get<std::uint64_t>();
        if (!networkSessionSendEnvelope(host, *session, std::move(*envelope)))
          session->game.reject(
              "failed to send predicted input; awaiting server correction");
        return sol::make_object(state, sequence);
      });
  networkSession.set_function(
      "prediction_state", [state, session](const std::string &id) {
        return jsonToLuaObject(state, session->protocol.prediction().state(id));
      });
  networkSession.set_function(
      "prediction_visual_offset",
      [state, &host, session](const std::string &id) {
        return jsonToLuaObject(
            state,
            session->protocol.prediction().visualOffset(id, host.gameTime()));
      });
  networkSession.set_function("remote_state", [state, &host,
                                               session](const std::string &id) {
    return jsonToLuaObject(
        state, session->protocol.prediction().remoteState(id, host.gameTime()));
  });
  networkSession.set_function("prediction_diagnostics", [state, session] {
    return jsonToLuaObject(state, session->protocol.prediction().diagnostics());
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
          session->protocol.connected();
        if (host.networkIsHost()) {
          const std::string assignedPeer =
              "peer" + std::to_string(event.peerId);
          if (const NetworkContract *contract = host.networkContract()) {
            const nlohmann::json metadata =
                session->sessionMetadata.valid() &&
                        session->sessionMetadata != sol::nil
                    ? luaObjectToJson(session->sessionMetadata)
                    : nlohmann::json{};
            for (auto &envelope :
                 session->protocol.lateJoin(*contract, assignedPeer, metadata))
              (void)networkSessionSendEnvelope(
                  host, *session, std::move(envelope), event.peerId);
          }
        }
      } else if (event.type == NetworkEventType::Disconnected) {
        summary["disconnected"] = true;
        if (host.networkIsHost() && host.networkContract() != nullptr) {
          const std::string peer = session->game.peerName(event.peerId);
          for (auto &envelope : session->protocol.disconnectPeer(
                   *host.networkContract(), peer)) {
            if (envelope.kind == NetworkEnvelopeKind::Despawn)
              session->localNetworkEntities.erase(envelope.target);
            (void)networkSessionSendEnvelope(host, *session,
                                             std::move(envelope));
          }
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
          const std::string trustedSender =
              host.networkIsHost() ? session->game.peerName(event.peerId)
                                   : "server";
          const NetworkGatewayResult accepted =
              session->protocol.receive(host.networkContract(), event.message,
                                        trustedSender, host.gameTime());
          if (!accepted.accepted)
            continue;
          const NetworkEnvelope &envelope = *accepted.envelope;
          if (envelope.kind == NetworkEnvelopeKind::Session &&
              !host.networkIsHost() && envelope.name == "session_start") {
            sol::object payloadObject = jsonToLuaObject(state, envelope.data);
            session->sessionMetadata = payloadObject;
            summary["session_started"] = true;
            summary["session"] = payloadObject;
          } else if (envelope.kind == NetworkEnvelopeKind::Message) {
            if (session->protocol.prediction().acceptInput(
                    *host.networkContract(), envelope, trustedSender,
                    host.gameTime()))
              continue;
            if (envelope.name == "state_update") {
              sol::object payloadObject = jsonToLuaObject(state, envelope.data);
              if (!payloadObject.is<sol::table>() ||
                  !networkSessionCreateOrApplyRemote(
                      state, host, *session, payloadObject.as<sol::table>()))
                continue;
              if (host.networkIsHost())
                (void)networkSessionSendEnvelope(host, *session, envelope);
              continue;
            }
            session->gameEvents.push_back(
                session->protocol.gameEvent(envelope, trustedSender));
            if (auto relayed = session->protocol.relayMessage(
                    *host.networkContract(), envelope, trustedSender))
              (void)networkSessionSendEnvelope(host, *session,
                                               std::move(*relayed));
          } else if (envelope.kind == NetworkEnvelopeKind::Spawn) {
            const auto *entity =
                session->protocol.ownership().find(envelope.target);
            if (entity != nullptr &&
                entity->ownerPeerId == networkSessionSenderId(*session))
              session->localNetworkEntities[entity->networkId] =
                  envelope.data.value("entity_id", std::string{});
            sol::object payloadObject = jsonToLuaObject(state, envelope.data);
            if (payloadObject.is<sol::table>())
              (void)networkSessionCreateOrApplyRemote(
                  state, host, *session, payloadObject.as<sol::table>());
          } else if (envelope.kind == NetworkEnvelopeKind::Despawn) {
            session->localNetworkEntities.erase(envelope.target);
            const std::string owner =
                envelope.data.value("owner", std::string{});
            if (!owner.empty()) {
              const std::string ghostId =
                  networkSessionRemoteId(owner, envelope.target);
              (void)host.destroyEntity(ghostId);
              session->remotes.erase(ghostId);
            }
          }
          continue;
        }
        session->game.reject("non-contract session packet rejected");
      }
    }
    summary["messages"] = messages;
    sol::table gameEvents = lua.create_table();
    int eventIndex = 1;
    for (const nlohmann::json &event : session->gameEvents)
      gameEvents[eventIndex++] = jsonToLuaObject(state, event);
    for (const auto &event : session->protocol.prediction().drainEvents())
      gameEvents[eventIndex++] = jsonToLuaObject(state, event);
    session->gameEvents.clear();
    summary["events"] = gameEvents;
    for (const auto &entity : session->protocol.ownership().snapshot()) {
      if (entity.ownerPeerId == networkSessionSenderId(*session))
        continue;
      const auto sample = session->protocol.prediction().remoteState(
          entity.networkId, host.gameTime());
      if (!sample.is_object() || !sample.contains("x") || !sample.contains("y"))
        continue;
      sol::table snapshot = lua.create_table();
      snapshot["sender_id"] = entity.ownerPeerId;
      snapshot["entity_id"] = entity.networkId;
      snapshot["x"] = sample.value("x", 0.0);
      snapshot["y"] = sample.value("y", 0.0);
      snapshot["vx"] = sample.value("vx", 0.0);
      snapshot["vy"] = sample.value("vy", 0.0);
      networkSessionApplySnapshot(host, *session, snapshot, true);
    }
    for (const auto &[ghostId, remote] : session->remotes) {
      const auto [x, y] =
          remote.motion.position(host.gameTime(), session->extrapolationLimit,
                                 session->initialPrediction);
      (void)host.setEntityPosition(ghostId, x, y);
    }
    return summary;
  });
  networkSession.set_function("update_entity", [&host, session](
                                                   const std::string &networkId,
                                                   const float dt) {
    if (!host.networkAvailable())
      return true;
    if (const NetworkContract *contract = host.networkContract()) {
      const NetworkOwnedEntity *owned =
          session->protocol.ownership().find(networkId);
      const auto local = session->localNetworkEntities.find(networkId);
      if (owned == nullptr || local == session->localNetworkEntities.end())
        return false;
      const auto publication = session->protocol.publicationStatus(
          networkId, dt, session->sendInterval);
      if (publication == NetworkPublicationStatus::Rejected)
        return false;
      if (publication == NetworkPublicationStatus::Waiting)
        return true;
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
    session->game.reject("entity replication requires a network contract");
    return false;
  });
}

} // namespace demi::runtime
