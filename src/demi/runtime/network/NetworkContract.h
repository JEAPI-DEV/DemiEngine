#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace demi {
struct AssetRegistry;
}

namespace demi::runtime {

enum class NetworkActor { Server, Owner, All };
enum class NetworkReliability { Reliable, Unreliable };
enum class NetworkDisconnectPolicy {
  Despawn,
  ReturnToServer,
  TransferByGamePolicy,
};

struct NetworkContractLimits {
  std::size_t maximumMessageBytes = 4096;
  std::uint32_t maximumMessagesPerSecond = 60;
  std::uint32_t maximumOwnedEntitiesPerPeer = 4;
  std::uint32_t maximumPayloadDepth = 12;
  std::uint32_t maximumPayloadElements = 256;
  std::size_t maximumStringBytes = 1024;
};

struct NetworkFieldRule {
  NetworkActor writeBy = NetworkActor::Server;
  std::string visibleTo = "all";
  std::uint32_t rate = 20;
  NetworkReliability reliability = NetworkReliability::Unreliable;
};

struct NetworkPrefabRule {
  std::string prefab;
  NetworkActor spawnBy = NetworkActor::Server;
  NetworkActor defaultOwner = NetworkActor::Server;
  NetworkActor transferBy = NetworkActor::Server;
  NetworkDisconnectPolicy onDisconnect = NetworkDisconnectPolicy::Despawn;
  std::map<std::string, NetworkFieldRule> fields;
};

struct NetworkMessageRule {
  NetworkActor from = NetworkActor::Server;
  NetworkActor to = NetworkActor::All;
  std::string target = "none";
  std::string schema;
  nlohmann::json schemaDocument;
  NetworkReliability reliability = NetworkReliability::Reliable;
  std::uint32_t rateLimit = 30;
  std::size_t maximumBytes = 1024;
};

struct NetworkContract {
  int formatVersion = 1;
  std::string id;
  std::string compatibilityHash;
  NetworkContractLimits limits;
  std::map<std::string, NetworkPrefabRule> replicatedPrefabs;
  std::map<std::string, NetworkMessageRule> messages;
};

struct NetworkContractLoadResult {
  std::optional<NetworkContract> contract;
  Diagnostics diagnostics;
};

[[nodiscard]] NetworkContractLoadResult
parseNetworkContract(const std::filesystem::path &path,
                     const std::string &jsonText,
                     const AssetRegistry *assets = nullptr);
[[nodiscard]] NetworkContractLoadResult
loadNetworkContract(const AssetRegistry &assets, const std::string &assetId);

[[nodiscard]] std::string_view networkActorName(NetworkActor actor);
[[nodiscard]] std::string_view
networkDisconnectPolicyName(NetworkDisconnectPolicy policy);

} // namespace demi::runtime
