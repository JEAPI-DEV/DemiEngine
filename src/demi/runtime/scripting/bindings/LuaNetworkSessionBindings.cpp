#include "demi/runtime/scripting/bindings/LuaNetworkSessionBindings.h"

#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>

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
  float accumulator = 0.0F;
  std::string localPeerId;
  Color localColor = {1.0F, 1.0F, 1.0F, 1.0F};
  sol::object sessionMetadata = sol::nil;
  sol::table remotePrefab;
  std::unordered_map<std::string, NetworkSessionRemote> remotes;
  std::unordered_map<std::string, NetworkSessionClaimObject> claimObjects;
  std::unordered_map<std::string, std::string> claimedObjects;
};

std::string networkSessionSenderId(LuaScriptHost& host, const NetworkSessionState& session) {
  if (host.networkIsHost()) {
    return "host";
  }
  return session.localPeerId.empty() ? "client" : session.localPeerId;
}

std::string networkSessionRemoteId(const sol::table snapshot) {
  return "net_" + snapshot.get_or("sender_id", std::string{}) + "_" + snapshot.get_or("entity_id", std::string{});
}

void networkSessionReset(LuaScriptHost& host, NetworkSessionState& session, const bool clearRemoteEntities) {
  if (clearRemoteEntities) {
    for (const auto& [ghostId, _] : session.remotes) {
      (void)host.destroyEntity(ghostId);
    }
  }
  session.localPeerId.clear();
  session.accumulator = 0.0F;
  session.remotes.clear();
  session.claimObjects.clear();
  session.claimedObjects.clear();
  session.sessionMetadata = sol::object{};
}

sol::table networkSessionClaimSyncPayload(lua_State* state, const NetworkSessionState& session) {
  sol::state_view lua(state);
  sol::table payload = lua.create_table();
  sol::table claims = lua.create_table();
  int index = 1;
  for (const auto& [objectId, collectorId] : session.claimedObjects) {
    sol::table claim = lua.create_table();
    claim["object_id"] = objectId;
    claim["collector_id"] = collectorId;
    claims[index++] = claim;
  }
  payload["claims"] = claims;
  return payload;
}

bool networkSessionSendMessage(lua_State* state, LuaScriptHost& host, const std::string& type, const sol::object payload, const bool reliable = true, const std::uint32_t peerId = 0, const std::uint8_t channel = 0) {
  (void)state;
  return host.networkSend(encodeNetworkMessage(type, sol::optional<sol::object>(payload)), reliable, channel, peerId);
}

bool networkSessionSendClaimSync(lua_State* state, LuaScriptHost& host, const NetworkSessionState& session, const std::uint32_t peerId = 0) {
  if (!host.networkAvailable() || !host.networkIsHost()) {
    return false;
  }
  sol::object payload = sol::make_object(state, networkSessionClaimSyncPayload(state, session));
  return networkSessionSendMessage(state, host, "claim_once_sync", payload, true, peerId, 0);
}

void networkSessionApplySnapshot(LuaScriptHost& host, NetworkSessionState& session, const sol::table snapshot) {
  const std::string senderId = snapshot.get_or("sender_id", std::string{});
  if (senderId.empty() || senderId == networkSessionSenderId(host, session) || !snapshot["x"].valid() || !snapshot["y"].valid()) {
    return;
  }

  const std::string ghostId = networkSessionRemoteId(snapshot);
  const bool needsCreate = !session.remotes.contains(ghostId) || !host.findEntityId(ghostId).has_value();
  if (needsCreate) {
    if (session.remotePrefab.valid()) {
      Entity entity;
      entity.id = ghostId;
      entity.name = session.remotePrefab.get_or("name", std::string("Network Ghost"));
      entity.transform2D = Transform2DComponent{
        .parent = session.remotePrefab.get_or("parent", std::string{}),
        .position = Vec2{.x = snapshot.get_or("x", 0.0F), .y = snapshot.get_or("y", 0.0F)},
        .rotation = session.remotePrefab.get_or("rotation", 0.0F),
        .scale = luaVec2Field(session.remotePrefab, "scale", {1.0F, 1.0F}),
      };
      entity.sprite = SpriteComponent{
        .texture = session.remotePrefab.get_or("texture", std::string{}),
        .layer = session.remotePrefab.get_or("layer", std::string("network")),
        .color = luaColorField(snapshot, "color", luaColorField(session.remotePrefab, "color")),
      };
      (void)host.createEntity(std::move(entity));
    }
    session.remotes[ghostId] = NetworkSessionRemote{.senderId = senderId};
  }

  NetworkSessionRemote& remote = session.remotes[ghostId];
  remote.senderId = senderId;
  remote.x = snapshot.get_or("x", 0.0F);
  remote.y = snapshot.get_or("y", 0.0F);
  remote.vx = snapshot.get_or("vx", 0.0F);
  remote.vy = snapshot.get_or("vy", 0.0F);
  remote.age = 0.0F;
  if (snapshot["color"].is<sol::table>()) {
    (void)host.setEntitySpriteColor(ghostId, luaColorField(snapshot, "color", luaColorField(session.remotePrefab, "color")));
  }
  (void)host.setEntityPosition(ghostId, remote.x + remote.vx * session.initialPrediction, remote.y + remote.vy * session.initialPrediction);
}

bool networkSessionApplyClaimOnce(lua_State* state, LuaScriptHost& host, NetworkSessionState& session, const std::string& id, const std::string& collectorId, const bool broadcast, const sol::object claim) {
  if (id.empty() || session.claimedObjects.contains(id)) {
    return false;
  }

  auto object = session.claimObjects.find(id);
  if (broadcast && object != session.claimObjects.end() && object->second.canClaim.valid()) {
    const sol::protected_function canClaim = object->second.canClaim;
    const sol::protected_function_result result = canClaim(id, collectorId, claim);
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
    if (collectorId == networkSessionSenderId(host, session) && object->second.onClaimedLocal.valid()) {
      const sol::protected_function onClaimedLocal = object->second.onClaimedLocal;
      (void)onClaimedLocal(id, collectorId);
    }
  }

  if (broadcast && host.networkAvailable() && host.networkIsHost()) {
    sol::state_view lua(state);
    sol::table payload = lua.create_table();
    payload["object_id"] = id;
    payload["collector_id"] = collectorId;
    networkSessionSendMessage(state, host, "claim_once_claimed", sol::make_object(state, payload), true, 0, 0);
  }
  return true;
}

} // namespace

void LuaNetworkSessionBindingModule::install(LuaScriptHost& host, lua_State* state) const {
  sol::state_view lua(state);
  auto ownedSession = std::make_unique<NetworkSessionState>();
  NetworkSessionState* session = ownedSession.get();
  sol::table networkSession = lua.create_named_table("NetworkSession");
  networkSession["_state"] = std::move(ownedSession);
  networkSession.set_function("configure", [session](const sol::table options) {
      session->sendInterval = options.get_or("send_interval", session->sendInterval);
      session->extrapolationLimit = options.get_or("extrapolation_limit", session->extrapolationLimit);
      session->initialPrediction = options.get_or("initial_prediction", session->initialPrediction);
      session->channel = static_cast<std::uint8_t>(std::max(options.get_or("channel", static_cast<int>(session->channel)), 0));
      session->defaultPort = static_cast<std::uint16_t>(std::max(options.get_or("port", static_cast<int>(session->defaultPort)), 0));
      session->maxPeers = static_cast<std::uint32_t>(std::max(options.get_or("max_peers", static_cast<int>(session->maxPeers)), 1));
      const sol::object remotePrefab = options["remote_prefab"];
      if (remotePrefab.is<sol::table>()) {
        session->remotePrefab = remotePrefab.as<sol::table>();
      }
    });
  networkSession.set_function("sender_id", [&host, session] { return networkSessionSenderId(host, *session); });
  networkSession.set_function("set_local_color", [session](float r, float g, float b, sol::optional<float> a) {
      session->localColor = Color{.r = r, .g = g, .b = b, .a = a.value_or(1.0F)};
    });
  networkSession.set_function("host", [&host, session](sol::optional<int> port) {
      if (!host.networkAvailable()) {
        return false;
      }
      networkSessionReset(host, *session, true);
      return host.networkHost(static_cast<std::uint16_t>(std::max(port.value_or(session->defaultPort), 0)), session->maxPeers);
    });
  networkSession.set_function("connect", [&host, session](sol::optional<std::string> address, sol::optional<int> port) {
      if (!host.networkAvailable()) {
        return false;
      }
      networkSessionReset(host, *session, true);
      return host.networkConnect(address.value_or("127.0.0.1"), static_cast<std::uint16_t>(std::max(port.value_or(session->defaultPort), 0)));
    });
  networkSession.set_function("disconnect", [&host, session] {
      host.networkDisconnect();
      networkSessionReset(host, *session, true);
    });
  networkSession.set_function("is_connected", [&host] { return host.networkIsConnected(); });
  networkSession.set_function("start_session", [state, &host, session](const sol::object metadata) {
      session->sessionMetadata = metadata;
      if (metadata.valid() && metadata != sol::nil && host.networkAvailable() && host.networkIsHost()) {
        networkSessionSendMessage(state, host, "session_start", metadata, true, 0, 0);
      }
    });
  networkSession.set_function("current_session", [session] { return session->sessionMetadata; });
  networkSession.set_function("reset_claims", [session] {
      session->claimObjects.clear();
      session->claimedObjects.clear();
    });
  networkSession.set_function("remote_position", [state, session](const std::string& senderId) {
      const auto best = std::ranges::min_element(session->remotes, [&](const auto& left, const auto& right) {
        if (left.second.senderId != senderId) {
          return false;
        }
        if (right.second.senderId != senderId) {
          return true;
        }
        return left.second.age < right.second.age;
      });
      if (best == session->remotes.end() || best->second.senderId != senderId) {
        return std::tuple<sol::object, sol::object>{sol::nil, sol::nil};
      }
      return std::tuple<sol::object, sol::object>{sol::make_object(state, best->second.x), sol::make_object(state, best->second.y)};
    });
  networkSession.set_function("register_claim_once", [session](const std::string& id, sol::optional<sol::table> options) {
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
  networkSession.set_function("apply_claim_once", [state, &host, session](const std::string& id, const std::string& collectorId, sol::optional<bool> broadcast, sol::optional<sol::object> claim) {
      return networkSessionApplyClaimOnce(state, host, *session, id, collectorId, broadcast.value_or(false), claim.value_or(sol::make_object(state, sol::nil)));
    });
  networkSession.set_function("request_claim_once_sync", [state, &host](sol::optional<int> peerId) {
      if (!host.networkAvailable()) {
        return false;
      }
      sol::state_view lua(state);
      return host.networkSend(encodeNetworkMessage("claim_once_sync_request", sol::make_object(state, lua.create_table())), true, 0, static_cast<std::uint32_t>(std::max(peerId.value_or(0), 0)));
    });
  networkSession.set_function("try_claim_once", [state, &host, session](const std::string& id, sol::optional<sol::object> claim) {
      auto object = session->claimObjects.find(id);
      if (object == session->claimObjects.end() || object->second.pending || session->claimedObjects.contains(id)) {
        return false;
      }
      const sol::object claimObject = claim.value_or(sol::make_object(state, sol::nil));
      if (!host.networkAvailable() || host.networkIsHost() || !host.networkIsConnected()) {
        return networkSessionApplyClaimOnce(state, host, *session, id, networkSessionSenderId(host, *session), true, claimObject);
      }
      object->second.pending = true;
      sol::state_view lua(state);
      sol::table payload = claimObject.is<sol::table>() ? claimObject.as<sol::table>() : lua.create_table();
      payload["object_id"] = id;
      return networkSessionSendMessage(state, host, "claim_once_request", sol::make_object(state, payload), true, 0, 0);
    });
  networkSession.set_function("process_events", [state, &host, session] {
      sol::state_view lua(state);
      sol::table summary = lua.create_table();
      summary["connected"] = false;
      summary["disconnected"] = false;
      summary["session_started"] = false;
      summary["session"] = sol::nil;
      summary["messages"] = 0;
      if (!host.networkAvailable()) {
        return summary;
      }
      int messages = 0;
      for (const NetworkEvent& event : host.networkDrainEvents()) {
        if (event.type == NetworkEventType::Connected) {
          summary["connected"] = true;
          if (host.networkIsHost()) {
            sol::table assign = lua.create_table();
            assign["peer_id"] = "peer" + std::to_string(event.peerId);
            networkSessionSendMessage(state, host, "assign_peer", sol::make_object(state, assign), true, event.peerId, 0);
            if (session->sessionMetadata.valid() && session->sessionMetadata != sol::nil) {
              networkSessionSendMessage(state, host, "session_start", session->sessionMetadata, true, event.peerId, 0);
            }
            networkSessionSendClaimSync(state, host, *session, event.peerId);
          }
        } else if (event.type == NetworkEventType::Disconnected) {
          summary["disconnected"] = true;
          if (host.networkIsHost()) {
            const std::string senderId = "peer" + std::to_string(event.peerId);
            for (auto it = session->remotes.begin(); it != session->remotes.end();) {
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
          const sol::object decoded = decodeNetworkMessage(state, event.message);
          if (!decoded.is<sol::table>()) {
            continue;
          }
          const sol::table message = decoded.as<sol::table>();
          const std::string type = message.get_or("type", std::string{});
          const sol::object payloadObject = message["payload"];
          sol::table payload = payloadObject.is<sol::table>() ? payloadObject.as<sol::table>() : lua.create_table();
          if (type == "assign_peer") {
            session->localPeerId = payload.get_or("peer_id", std::string{});
          } else if (type == "session_start") {
            session->sessionMetadata = payloadObject;
            summary["session_started"] = true;
            summary["session"] = payloadObject;
          } else if (type == "transform_snapshot") {
            if (host.networkIsHost() && payload.get_or("sender_id", std::string{}) != "host") {
              payload["sender_id"] = "peer" + std::to_string(event.peerId);
              networkSessionSendMessage(state, host, "transform_snapshot", sol::make_object(state, payload), false, 0, session->channel);
            }
            networkSessionApplySnapshot(host, *session, payload);
          } else if (type == "claim_once_request" && host.networkIsHost()) {
            const std::string objectId = payload.get_or("object_id", std::string{});
            const std::string collectorId = "peer" + std::to_string(event.peerId);
            if (const auto claimed = session->claimedObjects.find(objectId); claimed != session->claimedObjects.end()) {
              sol::table claimedPayload = lua.create_table();
              claimedPayload["object_id"] = objectId;
              claimedPayload["collector_id"] = claimed->second;
              networkSessionSendMessage(state, host, "claim_once_claimed", sol::make_object(state, claimedPayload), true, event.peerId, 0);
            } else if (!networkSessionApplyClaimOnce(state, host, *session, objectId, collectorId, true, payloadObject)) {
              sol::table rejectedPayload = lua.create_table();
              rejectedPayload["object_id"] = objectId;
              rejectedPayload["collector_id"] = collectorId;
              networkSessionSendMessage(state, host, "claim_once_rejected", sol::make_object(state, rejectedPayload), true, event.peerId, 0);
            }
          } else if (type == "claim_once_claimed") {
            networkSessionApplyClaimOnce(state, host, *session, payload.get_or("object_id", std::string{}), payload.get_or("collector_id", std::string{}), false, payloadObject);
          } else if (type == "claim_once_rejected") {
            if (auto object = session->claimObjects.find(payload.get_or("object_id", std::string{})); object != session->claimObjects.end()) {
              object->second.pending = false;
            }
          } else if (type == "claim_once_sync") {
            const sol::object claimsObject = payload["claims"];
            if (claimsObject.is<sol::table>()) {
              const sol::table claims = claimsObject.as<sol::table>();
              for (const auto& [_, claimObject] : claims) {
                if (claimObject.is<sol::table>()) {
                  const sol::table claim = claimObject.as<sol::table>();
                  networkSessionApplyClaimOnce(state, host, *session, claim.get_or("object_id", std::string{}), claim.get_or("collector_id", std::string{}), false, claimObject);
                }
              }
            }
          } else if (type == "claim_once_sync_request" && host.networkIsHost()) {
            networkSessionSendClaimSync(state, host, *session, event.peerId);
          }
        }
      }
      summary["messages"] = messages;
      return summary;
    });
  networkSession.set_function("update_local_transform", [state, &host, session](const std::string& entityId, const float dt) {
      if (!host.networkAvailable()) {
        return;
      }
      for (auto& [ghostId, remote] : session->remotes) {
        remote.age = std::min(remote.age + dt, session->extrapolationLimit);
        (void)host.setEntityPosition(ghostId, remote.x + remote.vx * remote.age, remote.y + remote.vy * remote.age);
      }
      if (!host.networkIsHost() && !host.networkIsConnected()) {
        return;
      }
      session->accumulator += dt;
      if (session->accumulator < session->sendInterval) {
        return;
      }
      session->accumulator = 0.0F;
      const std::optional<Vec2> position = host.entityPosition(entityId);
      if (!position.has_value()) {
        return;
      }
      const std::optional<Vec2> velocity = host.getRigidbodyVelocity(entityId);
      sol::state_view lua(state);
      sol::table payload = lua.create_table();
      payload["sender_id"] = networkSessionSenderId(host, *session);
      payload["entity_id"] = entityId;
      payload["x"] = position->x;
      payload["y"] = position->y;
      payload["vx"] = velocity.has_value() ? velocity->x : 0.0F;
      payload["vy"] = velocity.has_value() ? velocity->y : 0.0F;
      sol::table color = lua.create_table();
      color[1] = session->localColor.r;
      color[2] = session->localColor.g;
      color[3] = session->localColor.b;
      color[4] = session->localColor.a;
      payload["color"] = color;
      networkSessionSendMessage(state, host, "transform_snapshot", sol::make_object(state, payload), false, 0, session->channel);
    });
}


} // namespace demi::runtime
