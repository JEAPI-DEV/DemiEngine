#include "demi/runtime/scripting/bindings/LuaNetworkBindings.h"

#include "demi/runtime/network/HttpClient.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

#include <algorithm>
#include <map>
#include <vector>

namespace demi::runtime {

namespace {

const char* networkEventTypeName(const NetworkEventType type) {
  switch (type) {
  case NetworkEventType::Connected:
    return "connected";
  case NetworkEventType::Disconnected:
    return "disconnected";
  case NetworkEventType::Message:
    return "message";
  }
  return "unknown";
}

sol::table networkEventsTable(lua_State* state, const std::vector<NetworkEvent>& events) {
  sol::state_view lua(state);
  sol::table result = lua.create_table();
  int index = 1;
  for (const NetworkEvent& event : events) {
    sol::table item = lua.create_table();
    item["type"] = networkEventTypeName(event.type);
    item["peer_id"] = event.peerId;
    item["channel"] = event.channel;
    item["message"] = event.message;
    item["latency_ms"] = event.latencyMs;
    result[index++] = item;
  }
  return result;
}

std::map<std::string, std::string> stringMapFromTable(const sol::table table) {
  std::map<std::string, std::string> fields;
  for (const auto& [key, value] : table) {
    if (!key.is<std::string>()) {
      continue;
    }
    if (value.is<std::string>()) {
      fields[key.as<std::string>()] = value.as<std::string>();
    } else if (value.is<int>()) {
      fields[key.as<std::string>()] = std::to_string(value.as<int>());
    } else if (value.is<double>()) {
      fields[key.as<std::string>()] = std::to_string(value.as<double>());
    } else if (value.is<bool>()) {
      fields[key.as<std::string>()] = value.as<bool>() ? "1" : "0";
    }
  }
  return fields;
}

sol::table httpResponseTable(lua_State* state, const HttpResponse& response) {
  sol::state_view lua(state);
  sol::table result = lua.create_table();
  result["ok"] = response.ok;
  result["status"] = response.status;
  result["body"] = response.body;
  result["error"] = response.error;
  if (!response.body.empty()) {
    try {
      result["json"] = jsonToLuaObject(state, nlohmann::json::parse(response.body));
    } catch (...) {
      result["json"] = sol::nil;
    }
  }
  return result;
}

sol::table lobbyPost(lua_State* state, const std::string& url, const std::map<std::string, std::string>& fields, const int timeoutMs) {
  return httpResponseTable(state, httpPostForm(url, fields, std::max(timeoutMs, 1)));
}

} // namespace

void LuaNetworkBindingModule::install(LuaScriptHost& host, lua_State* state) const {
  sol::state_view lua(state);
  sol::table network = lua.create_named_table("Network");
  network.set_function("available", [&host] { return host.networkAvailable(); });
  network.set_function("host", [&host](int port, sol::optional<int> maxPeers) { return host.networkHost(static_cast<std::uint16_t>(std::max(port, 0)), static_cast<std::uint32_t>(std::max(maxPeers.value_or(8), 1))); });
  network.set_function("host_dtls", [&host](int port, const std::string& certificate, const std::string& privateKey, sol::optional<int> maxPeers) {
      return host.networkHostSecure(static_cast<std::uint16_t>(std::max(port, 0)), certificate, privateKey, static_cast<std::uint32_t>(std::max(maxPeers.value_or(8), 1)));
    });
  network.set_function("connect", [&host](const std::string& address, int port) { return host.networkConnect(address, static_cast<std::uint16_t>(std::max(port, 0))); });
  network.set_function("connect_dtls", [&host](const std::string& address, int port, const std::string& trustedCertificate, sol::optional<std::string> serverName) {
      return host.networkConnectSecure(address, static_cast<std::uint16_t>(std::max(port, 0)), trustedCertificate, serverName.value_or(address));
    });
  network.set_function("disconnect", [&host] { host.networkDisconnect(); });
  network.set_function("disconnect_peer", [&host](int peerId) { host.networkDisconnectPeer(static_cast<std::uint32_t>(std::max(peerId, 0))); });
  network.set_function("send", [&host](const std::string& message, sol::optional<bool> reliable, sol::optional<int> peerId, sol::optional<int> channel) {
      return host.networkSend(message, reliable.value_or(true), static_cast<std::uint8_t>(std::max(channel.value_or(0), 0)), static_cast<std::uint32_t>(std::max(peerId.value_or(0), 0)));
    });
  network.set_function("is_host", [&host] { return host.networkIsHost(); });
  network.set_function("is_connected", [&host] { return host.networkIsConnected(); });
  network.set_function("is_secure", [&host] { return host.networkIsSecure(); });
  network.set_function("security_error", [&host] { return host.networkSecurityError(); });
  network.set_function("latency_ms", [&host] { return host.networkLatencyMs(); });
  network.set_function("events", [state, &host] { return networkEventsTable(state, host.networkDrainEvents()); });
  network.set_function("http_get", [state](const std::string& url, sol::optional<int> timeoutMs) {
      return httpResponseTable(state, httpGet(url, std::max(timeoutMs.value_or(5000), 1)));
    });
  network.set_function("http_post_form", [state](const std::string& url, const sol::table fields, sol::optional<int> timeoutMs) {
      return httpResponseTable(state, httpPostForm(url, stringMapFromTable(fields), std::max(timeoutMs.value_or(5000), 1)));
    });
  network.set_function("lobby_list", [state](const std::string& url, sol::optional<std::string> game, sol::optional<int> timeoutMs) {
      std::map<std::string, std::string> fields{{"action", "list"}};
      if (game.has_value() && !game->empty()) {
        fields["game"] = *game;
      }
      return lobbyPost(state, url, fields, timeoutMs.value_or(5000));
    });
  network.set_function("lobby_create", [state](const std::string& url, sol::optional<std::string> game, int port, sol::optional<std::string> playerName, sol::optional<int> timeoutMs) {
      std::map<std::string, std::string> fields{
        {"action", "create"},
        {"port", std::to_string(std::max(port, 0))},
      };
      if (game.has_value() && !game->empty()) {
        fields["game"] = *game;
      }
      if (playerName.has_value() && !playerName->empty()) {
        fields["player_name"] = *playerName;
      }
      return lobbyPost(state, url, fields, timeoutMs.value_or(5000));
    });
  network.set_function("lobby_join", [state](const std::string& url, int lobbyId, sol::optional<std::string> playerName, sol::optional<int> timeoutMs) {
      std::map<std::string, std::string> fields{
        {"action", "join"},
        {"lobby_id", std::to_string(std::max(lobbyId, 0))},
      };
      if (playerName.has_value() && !playerName->empty()) {
        fields["player_name"] = *playerName;
      }
      return lobbyPost(state, url, fields, timeoutMs.value_or(5000));
    });
  network.set_function("lobby_heartbeat", [state](const std::string& url, int lobbyId, const std::string& playerToken, sol::optional<int> timeoutMs) {
      return lobbyPost(state, url, {{"action", "heartbeat"}, {"lobby_id", std::to_string(std::max(lobbyId, 0))}, {"player_token", playerToken}}, timeoutMs.value_or(5000));
    });
  network.set_function("lobby_leave", [state](const std::string& url, int lobbyId, const std::string& playerToken, sol::optional<int> timeoutMs) {
      return lobbyPost(state, url, {{"action", "leave"}, {"lobby_id", std::to_string(std::max(lobbyId, 0))}, {"player_token", playerToken}}, timeoutMs.value_or(5000));
    });
  network.set_function("sender_id", [&host](sol::optional<std::string> assignedPeerId) {
      if (host.networkIsHost()) {
        return std::string("host");
      }
      return assignedPeerId.value_or("client");
    });
  network.set_function("encode", [](const std::string& type, sol::optional<sol::object> payload) { return encodeNetworkMessage(type, payload); });
  network.set_function("decode", [state](const std::string& message) { return decodeNetworkMessage(state, message); });
}

} // namespace demi::runtime
