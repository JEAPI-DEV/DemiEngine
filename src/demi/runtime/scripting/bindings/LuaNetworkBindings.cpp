#include "demi/runtime/scripting/bindings/LuaNetworkBindings.h"

#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <vector>

namespace demi::runtime {

#if DEMI_HAS_LUA54
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

} // namespace

void LuaNetworkBindingModule::install(LuaScriptHost& host, lua_State* state) const {
  sol::state_view lua(state);
  sol::table network = lua.create_named_table("Network");
  network.set_function("available", [&host] { return host.networkAvailable(); });
  network.set_function("host", [&host](int port, sol::optional<int> maxPeers) { return host.networkHost(static_cast<std::uint16_t>(std::max(port, 0)), static_cast<std::uint32_t>(std::max(maxPeers.value_or(8), 1))); });
  network.set_function("connect", [&host](const std::string& address, int port) { return host.networkConnect(address, static_cast<std::uint16_t>(std::max(port, 0))); });
  network.set_function("disconnect", [&host] { host.networkDisconnect(); });
  network.set_function("send", [&host](const std::string& message, sol::optional<bool> reliable, sol::optional<int> peerId, sol::optional<int> channel) {
      return host.networkSend(message, reliable.value_or(true), static_cast<std::uint8_t>(std::max(channel.value_or(0), 0)), static_cast<std::uint32_t>(std::max(peerId.value_or(0), 0)));
    });
  network.set_function("is_host", [&host] { return host.networkIsHost(); });
  network.set_function("is_connected", [&host] { return host.networkIsConnected(); });
  network.set_function("latency_ms", [&host] { return host.networkLatencyMs(); });
  network.set_function("events", [state, &host] { return networkEventsTable(state, host.networkDrainEvents()); });
  network.set_function("sender_id", [&host](sol::optional<std::string> assignedPeerId) {
      if (host.networkIsHost()) {
        return std::string("host");
      }
      return assignedPeerId.value_or("client");
    });
  network.set_function("encode", [](const std::string& type, sol::optional<sol::object> payload) { return encodeNetworkMessage(type, payload); });
  network.set_function("decode", [state](const std::string& message) { return decodeNetworkMessage(state, message); });
}
#endif

} // namespace demi::runtime
