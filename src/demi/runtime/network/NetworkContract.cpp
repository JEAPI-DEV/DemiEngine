#include "demi/runtime/network/NetworkContract.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <span>
#include <sstream>

namespace demi::runtime {
namespace {

using Json = nlohmann::json;

void error(Diagnostics &diagnostics, const std::filesystem::path &path,
           std::string code, std::string message, std::string suggestion = {}) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path.string(),
                         .suggestion = std::move(suggestion)});
}

std::optional<NetworkActor> actor(const Json &object, const char *field,
                                  Diagnostics &diagnostics,
                                  const std::filesystem::path &path,
                                  const NetworkActor fallback) {
  if (!object.contains(field))
    return fallback;
  if (!object[field].is_string()) {
    error(diagnostics, path, "NETWORK_CONTRACT_INVALID_ACTOR",
          std::string(field) + " must be server, owner, or all.");
    return std::nullopt;
  }
  const std::string value = object[field].get<std::string>();
  if (value == "server")
    return NetworkActor::Server;
  if (value == "owner")
    return NetworkActor::Owner;
  if (value == "all")
    return NetworkActor::All;
  error(diagnostics, path, "NETWORK_CONTRACT_INVALID_ACTOR",
        std::string(field) + " has unsupported actor: " + value);
  return std::nullopt;
}

std::optional<NetworkReliability>
reliability(const Json &object, Diagnostics &diagnostics,
            const std::filesystem::path &path,
            const NetworkReliability fallback) {
  if (!object.contains("reliability"))
    return fallback;
  if (!object["reliability"].is_string()) {
    error(diagnostics, path, "NETWORK_CONTRACT_INVALID_RELIABILITY",
          "reliability must be reliable or unreliable.");
    return std::nullopt;
  }
  const std::string value = object["reliability"].get<std::string>();
  if (value == "reliable")
    return NetworkReliability::Reliable;
  if (value == "unreliable")
    return NetworkReliability::Unreliable;
  error(diagnostics, path, "NETWORK_CONTRACT_INVALID_RELIABILITY",
        "Unsupported reliability: " + value);
  return std::nullopt;
}

std::uint32_t boundedUnsigned(const Json &object, const char *field,
                              const std::uint32_t fallback,
                              const std::uint32_t minimum,
                              const std::uint32_t maximum,
                              Diagnostics &diagnostics,
                              const std::filesystem::path &path) {
  if (!object.contains(field))
    return fallback;
  if (!object[field].is_number_unsigned() &&
      !object[field].is_number_integer()) {
    error(diagnostics, path, "NETWORK_CONTRACT_INVALID_LIMIT",
          std::string(field) + " must be an integer.");
    return fallback;
  }
  const auto value = object[field].get<std::int64_t>();
  if (value < static_cast<std::int64_t>(minimum) ||
      value > static_cast<std::int64_t>(maximum)) {
    error(diagnostics, path, "NETWORK_CONTRACT_INVALID_LIMIT",
          std::string(field) + " is outside the supported range.");
    return fallback;
  }
  return static_cast<std::uint32_t>(value);
}

void flattenFields(const Json &node, const std::string &component,
                   const std::string &prefix, NetworkPrefabRule &prefab,
                   Diagnostics &diagnostics,
                   const std::filesystem::path &path) {
  if (!node.is_object()) {
    error(diagnostics, path, "NETWORK_CONTRACT_INVALID_FIELD",
          component + "." + prefix + " must be an object.");
    return;
  }
  if (node.contains("write_by") || node.contains("visible_to") ||
      node.contains("reliability") || node.contains("rate")) {
    const auto writer = actor(node, "write_by", diagnostics, path,
                              NetworkActor::Server);
    const auto delivery = reliability(node, diagnostics, path,
                                      NetworkReliability::Unreliable);
    if (!writer || !delivery)
      return;
    NetworkFieldRule rule;
    rule.writeBy = *writer;
    rule.reliability = *delivery;
    rule.visibleTo = node.value("visible_to", "all");
    if (rule.visibleTo != "all" && rule.visibleTo != "owner" &&
        rule.visibleTo != "server" && rule.visibleTo != "owner_and_server") {
      error(diagnostics, path, "NETWORK_CONTRACT_INVALID_VISIBILITY",
            "Unsupported visibility for " + component + "." + prefix);
      return;
    }
    rule.rate = boundedUnsigned(node, "rate", 20, 1, 240, diagnostics, path);
    prefab.fields[component + "." + prefix] = std::move(rule);
    return;
  }
  for (const auto &[name, child] : node.items())
    flattenFields(child, component, prefix.empty() ? name : prefix + "." + name,
                  prefab, diagnostics, path);
}

bool validateReflectedField(const std::string &path) {
  const std::size_t dot = path.find('.');
  if (dot == std::string::npos)
    return false;
  const std::string component = path.substr(0, dot);
  const std::string field = path.substr(dot + 1, path.find('.', dot + 1) - dot - 1);
  const auto *descriptor = scene_loading::findComponentDescriptor(component);
  return descriptor != nullptr &&
         std::ranges::any_of(descriptor->fields, [&](const auto &candidate) {
           return candidate.name == field && candidate.replicated;
         });
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

} // namespace

NetworkContractLoadResult
parseNetworkContract(const std::filesystem::path &path,
                     const std::string &jsonText, const AssetRegistry *assets) {
  NetworkContractLoadResult result;
  Json document;
  try {
    document = Json::parse(jsonText);
  } catch (const Json::exception &exception) {
    error(result.diagnostics, path, "NETWORK_CONTRACT_INVALID_JSON",
          exception.what());
    return result;
  }
  if (!document.is_object()) {
    error(result.diagnostics, path, "NETWORK_CONTRACT_INVALID_ROOT",
          "Network contract root must be an object.");
    return result;
  }

  NetworkContract contract;
  contract.formatVersion = document.value("format_version", 0);
  contract.id = document.value("id", "");
  if (contract.formatVersion != 1)
    error(result.diagnostics, path, "NETWORK_CONTRACT_VERSION_UNSUPPORTED",
          "Only network contract format_version 1 is supported.");
  if (!contract.id.starts_with("network-contract://"))
    error(result.diagnostics, path, "NETWORK_CONTRACT_INVALID_ID",
          "Network contract id must start with network-contract://.");

  if (const auto limits = document.find("limits"); limits != document.end()) {
    if (!limits->is_object())
      error(result.diagnostics, path, "NETWORK_CONTRACT_INVALID_LIMITS",
            "limits must be an object.");
    else {
      contract.limits.maximumMessageBytes = boundedUnsigned(
          *limits, "maximum_message_bytes", 4096, 64, 1024 * 1024,
          result.diagnostics, path);
      contract.limits.maximumMessagesPerSecond = boundedUnsigned(
          *limits, "maximum_messages_per_second", 60, 1, 10000,
          result.diagnostics, path);
      contract.limits.maximumOwnedEntitiesPerPeer = boundedUnsigned(
          *limits, "maximum_owned_entities_per_peer", 4, 1, 10000,
          result.diagnostics, path);
      contract.limits.maximumPayloadDepth = boundedUnsigned(
          *limits, "maximum_payload_depth", 12, 1, 64, result.diagnostics,
          path);
      contract.limits.maximumPayloadElements = boundedUnsigned(
          *limits, "maximum_payload_elements", 256, 1, 100000,
          result.diagnostics, path);
      contract.limits.maximumStringBytes = boundedUnsigned(
          *limits, "maximum_string_bytes", 1024, 1, 1024 * 1024,
          result.diagnostics, path);
    }
  }

  const Json prefabs = document.value("replicated_prefabs", Json::object());
  if (!prefabs.is_object())
    error(result.diagnostics, path, "NETWORK_CONTRACT_INVALID_PREFABS",
          "replicated_prefabs must be an object.");
  else for (const auto &[name, value] : prefabs.items()) {
    if (!value.is_object()) {
      error(result.diagnostics, path, "NETWORK_CONTRACT_INVALID_PREFAB",
            "Replicated prefab " + name + " must be an object.");
      continue;
    }
    NetworkPrefabRule prefab;
    prefab.prefab = value.value("prefab", "");
    const auto spawnBy = actor(value, "spawn_by", result.diagnostics, path,
                               NetworkActor::Server);
    if (spawnBy)
      prefab.spawnBy = *spawnBy;
    if (prefab.spawnBy != NetworkActor::Server)
      error(result.diagnostics, path, "NETWORK_CONTRACT_CLIENT_SPAWN_FORBIDDEN",
            "Step 6 contracts require server-only spawn for " + name + ".");
    if (!prefab.prefab.starts_with("prefab://"))
      error(result.diagnostics, path, "NETWORK_CONTRACT_INVALID_PREFAB_REF",
            "Replicated prefab " + name + " needs a prefab:// reference.");
    else if (assets != nullptr) {
      const auto prefabPath =
          composition::resolvePrefabReference(path, prefab.prefab);
      if (!prefabPath || !std::filesystem::is_regular_file(*prefabPath))
        error(result.diagnostics, path, "NETWORK_CONTRACT_PREFAB_NOT_FOUND",
              "Replicated prefab does not exist: " + prefab.prefab);
    }
    if (const auto ownership = value.find("ownership");
        ownership != value.end() && ownership->is_object()) {
      if (const auto owner = actor(*ownership, "default", result.diagnostics,
                                   path, NetworkActor::Server))
        prefab.defaultOwner = *owner;
      if (const auto transfer = actor(*ownership, "transfer_by",
                                      result.diagnostics, path,
                                      NetworkActor::Server))
        prefab.transferBy = *transfer;
      if (prefab.transferBy != NetworkActor::Server)
        error(result.diagnostics, path,
              "NETWORK_CONTRACT_CLIENT_TRANSFER_FORBIDDEN",
              "Ownership transfer must be server-only for " + name + ".");
      const std::string policy = ownership->value("on_disconnect", "despawn");
      if (policy == "despawn")
        prefab.onDisconnect = NetworkDisconnectPolicy::Despawn;
      else if (policy == "return_to_server")
        prefab.onDisconnect = NetworkDisconnectPolicy::ReturnToServer;
      else if (policy == "transfer_by_game_policy")
        prefab.onDisconnect = NetworkDisconnectPolicy::TransferByGamePolicy;
      else
        error(result.diagnostics, path,
              "NETWORK_CONTRACT_INVALID_DISCONNECT_POLICY",
              "Unsupported disconnect policy for " + name + ": " + policy);
    }
    if (const auto components = value.find("components");
        components != value.end() && components->is_object())
      for (const auto &[component, fields] : components->items())
        for (const auto &[field, rule] : fields.items())
          flattenFields(rule, component, field, prefab, result.diagnostics,
                        path);
    for (const auto &[field, unused] : prefab.fields) {
      (void)unused;
      if (!validateReflectedField(field))
        error(result.diagnostics, path,
              "NETWORK_CONTRACT_FIELD_NOT_REPLICATED",
              field + " is unknown or not marked replicated by reflection.");
    }
    contract.replicatedPrefabs.emplace(name, std::move(prefab));
  }

  const Json messages = document.value("messages", Json::object());
  std::string compatibilityInput = document.dump();
  if (!messages.is_object())
    error(result.diagnostics, path, "NETWORK_CONTRACT_INVALID_MESSAGES",
          "messages must be an object.");
  else for (const auto &[name, value] : messages.items()) {
    if (!value.is_object() || name.empty()) {
      error(result.diagnostics, path, "NETWORK_CONTRACT_INVALID_MESSAGE",
            "Declared messages must be named objects.");
      continue;
    }
    NetworkMessageRule rule;
    const auto from = actor(value, "from", result.diagnostics, path,
                            NetworkActor::Server);
    const auto to = actor(value, "to", result.diagnostics, path,
                          NetworkActor::All);
    const auto delivery = reliability(value, result.diagnostics, path,
                                      NetworkReliability::Reliable);
    if (from) rule.from = *from;
    if (to) rule.to = *to;
    if (delivery) rule.reliability = *delivery;
    rule.target = value.value("target", "none");
    rule.schema = value.value("schema", "");
    rule.rateLimit = boundedUnsigned(value, "rate_limit", 30, 1, 10000,
                                     result.diagnostics, path);
    rule.maximumBytes = boundedUnsigned(
        value, "maximum_bytes", 1024, 1,
        static_cast<std::uint32_t>(contract.limits.maximumMessageBytes),
        result.diagnostics, path);
    if (rule.target != "none" && rule.target != "owned_entity" &&
        rule.target != "entity")
      error(result.diagnostics, path, "NETWORK_CONTRACT_INVALID_TARGET",
            "Unsupported target rule for message " + name + ".");
    if (!rule.schema.empty()) {
      const AssetManifest *schema = assets == nullptr
                                        ? nullptr
                                        : findAsset(*assets, rule.schema);
      if (assets != nullptr &&
          (schema == nullptr || schema->type != "DataSchema")) {
        error(result.diagnostics, path, "NETWORK_CONTRACT_SCHEMA_NOT_FOUND",
              "Message " + name + " references a missing DataSchema: " +
                  rule.schema);
      } else if (schema != nullptr) {
        try {
          rule.schemaDocument = Json::parse(readFile(schema->sourcePath));
          if (!rule.schemaDocument.is_object() ||
              rule.schemaDocument.value("format_version", 0) != 1)
            error(result.diagnostics, schema->sourcePath,
                  "NETWORK_CONTRACT_SCHEMA_INVALID",
                  "Network message schemas require format_version 1.");
          compatibilityInput += "\n" + rule.schema + "\n" +
                                rule.schemaDocument.dump();
        } catch (const Json::exception &exception) {
          error(result.diagnostics, schema->sourcePath,
                "NETWORK_CONTRACT_SCHEMA_INVALID", exception.what());
        }
      }
    }
    contract.messages.emplace(name, std::move(rule));
  }

  contract.compatibilityHash = assets::hashBytes(std::span(
      reinterpret_cast<const unsigned char *>(compatibilityInput.data()),
      compatibilityInput.size()));
  if (!hasErrors(result.diagnostics))
    result.contract = std::move(contract);
  return result;
}

NetworkContractLoadResult loadNetworkContract(const AssetRegistry &assets,
                                              const std::string &assetId) {
  const AssetManifest *manifest = findAsset(assets, assetId);
  if (manifest == nullptr || manifest->type != "NetworkContract") {
    NetworkContractLoadResult result;
    error(result.diagnostics, assets.projectDirectory,
          "NETWORK_CONTRACT_ASSET_NOT_FOUND",
          "NetworkContract asset was not found: " + assetId);
    return result;
  }
  return parseNetworkContract(manifest->sourcePath, readFile(manifest->sourcePath),
                              &assets);
}

std::string_view networkActorName(const NetworkActor actor) {
  switch (actor) {
  case NetworkActor::Server: return "server";
  case NetworkActor::Owner: return "owner";
  case NetworkActor::All: return "all";
  }
  return "server";
}

std::string_view networkDisconnectPolicyName(
    const NetworkDisconnectPolicy policy) {
  switch (policy) {
  case NetworkDisconnectPolicy::Despawn: return "despawn";
  case NetworkDisconnectPolicy::ReturnToServer: return "return_to_server";
  case NetworkDisconnectPolicy::TransferByGamePolicy:
    return "transfer_by_game_policy";
  }
  return "despawn";
}

} // namespace demi::runtime
