#include "demi/runtime/scripting/bindings/LuaTlsBindings.h"
#include "demi/runtime/network/TlsMessaging.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pkcs5.h>
#include <sol/sol.hpp>

#include <algorithm>
#include <array>
#include <memory>

namespace demi::runtime {
namespace {
struct TlsBindingState {
  TlsServer server;
  TlsClient client;
};
const char* eventName(TlsEventType type) {
  if (type == TlsEventType::Connected) return "connected";
  if (type == TlsEventType::Message) return "message";
  return "disconnected";
}
sol::table eventsTable(lua_State* state, std::vector<TlsEvent> events) {
  sol::state_view lua(state); sol::table result = lua.create_table(); int index = 1;
  for (TlsEvent& event : events) {
    sol::table item = lua.create_table();
    item["type"] = eventName(event.type); item["client_id"] = event.clientId; item["message"] = std::move(event.message);
    result[index++] = std::move(item);
  }
  return result;
}
std::string hex(const unsigned char* bytes, std::size_t size) {
  constexpr char Digits[] = "0123456789abcdef"; std::string result(size * 2, '0');
  for (std::size_t i = 0; i < size; ++i) { result[i * 2] = Digits[bytes[i] >> 4U]; result[i * 2 + 1] = Digits[bytes[i] & 15U]; }
  return result;
}
std::string randomToken(int byteCount) {
  byteCount = std::clamp(byteCount, 1, 64);
  mbedtls_entropy_context entropy; mbedtls_ctr_drbg_context random;
  mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&random);
  static constexpr unsigned char Personalization[] = "Demi Lua Crypto";
  std::array<unsigned char, 64> output{};
  const int seeded = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy, Personalization, sizeof(Personalization) - 1);
  const int generated = seeded == 0 ? mbedtls_ctr_drbg_random(&random, output.data(), byteCount) : seeded;
  mbedtls_ctr_drbg_free(&random); mbedtls_entropy_free(&entropy);
  return generated == 0 ? hex(output.data(), byteCount) : std::string{};
}
std::string passwordHash(const std::string& password, const std::string& salt, int iterations) {
  iterations = std::clamp(iterations, 1000, 1000000);
  std::array<unsigned char, 32> output{};
  const int result = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
    reinterpret_cast<const unsigned char*>(password.data()), password.size(),
    reinterpret_cast<const unsigned char*>(salt.data()), salt.size(),
    static_cast<unsigned int>(iterations), output.size(), output.data());
  return result == 0 ? hex(output.data(), output.size()) : std::string{};
}
bool secureEquals(const std::string& left, const std::string& right) {
  const std::size_t size = std::max(left.size(), right.size()); unsigned char difference = static_cast<unsigned char>(left.size() ^ right.size());
  for (std::size_t i = 0; i < size; ++i) difference |= static_cast<unsigned char>((i < left.size() ? left[i] : 0) ^ (i < right.size() ? right[i] : 0));
  return difference == 0;
}
}

void LuaTlsBindingModule::install(LuaScriptHost& host, lua_State* state) const {
  sol::state_view lua(state);
  auto owned = std::make_unique<TlsBindingState>();
  TlsServer* server = &owned->server;
  TlsClient* client = &owned->client;
  sol::table tlsServer = lua.create_named_table("TlsServer");
  tlsServer["_state"] = std::move(owned);
  tlsServer.set_function("listen", [&host, server](int port, const std::string& cert, const std::string& key, sol::optional<int> maxClients) {
    return server->listen(static_cast<std::uint16_t>(std::clamp(port, 1, 65535)), host.resolveProjectPath(cert), host.resolveProjectPath(key),
                          static_cast<std::uint32_t>(std::max(maxClients.value_or(64), 1)));
  });
  tlsServer.set_function("events", [state, server] { server->update(); return eventsTable(state, server->drainEvents()); });
  tlsServer.set_function("send", [server](int id, const std::string& message) { return server->send(static_cast<std::uint32_t>(std::max(id, 0)), message); });
  tlsServer.set_function("disconnect", [server](int id) { server->disconnect(static_cast<std::uint32_t>(std::max(id, 0))); });
  tlsServer.set_function("error", [server] { return server->lastError(); });

  sol::table tlsClient = lua.create_named_table("TlsClient");
  tlsClient.set_function("connect", [&host, client](const std::string& address, int port, const std::string& cert, sol::optional<std::string> serverName) {
    return client->connect(address, static_cast<std::uint16_t>(std::clamp(port, 1, 65535)), host.resolveProjectPath(cert), serverName.value_or(address));
  });
  tlsClient.set_function("events", [state, client] { client->update(); return eventsTable(state, client->drainEvents()); });
  tlsClient.set_function("send", [client](const std::string& message) { return client->send(message); });
  tlsClient.set_function("disconnect", [client] { client->disconnect(); });
  tlsClient.set_function("is_connected", [client] { return client->isConnected(); });
  tlsClient.set_function("error", [client] { return client->lastError(); });

  sol::table crypto = lua.create_named_table("Crypto");
  crypto.set_function("random_token", [](sol::optional<int> bytes) { return randomToken(bytes.value_or(32)); });
  crypto.set_function("password_hash", [](const std::string& password, const std::string& salt, sol::optional<int> iterations) {
    return passwordHash(password, salt, iterations.value_or(100000));
  });
  crypto.set_function("secure_equals", secureEquals);
}
}
