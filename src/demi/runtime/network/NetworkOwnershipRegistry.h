#pragma once

#include "demi/runtime/network/NetworkContract.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct NetworkOwnedEntity {
  std::string networkId;
  std::string prefabKey;
  std::string ownerPeerId;
  std::uint64_t sessionEpoch = 0;
  std::uint64_t ownershipGeneration = 1;
};

enum class OwnershipRejectCode {
  None,
  NotServer,
  UnknownPrefab,
  UnknownEntity,
  DuplicateEntity,
  InvalidOwner,
  EntityLimit,
  StaleEpoch,
  StaleGeneration,
};

struct OwnershipResult {
  bool accepted = false;
  OwnershipRejectCode code = OwnershipRejectCode::None;
  std::string reason;
  std::optional<NetworkOwnedEntity> entity;
};

struct DisconnectOwnershipActions {
  std::vector<NetworkOwnedEntity> despawned;
  std::vector<NetworkOwnedEntity> returnedToServer;
  std::vector<NetworkOwnedEntity> awaitingGamePolicy;
};

class NetworkOwnershipRegistry {
public:
  explicit NetworkOwnershipRegistry(bool authoritativeServer = false);

  void reset(bool authoritativeServer);
  [[nodiscard]] bool synchronizeEpoch(std::uint64_t authoritativeEpoch);
  [[nodiscard]] bool authoritativeServer() const;
  [[nodiscard]] std::uint64_t sessionEpoch() const;
  [[nodiscard]] std::size_t size() const;

  [[nodiscard]] OwnershipResult
  spawn(const NetworkContract &contract, std::string_view prefabKey,
        std::string ownerPeerId = "server");
  [[nodiscard]] OwnershipResult applyAuthoritativeSpawn(
      NetworkOwnedEntity entity);
  [[nodiscard]] OwnershipResult transfer(const NetworkContract &contract,
                                         std::string_view networkId,
                                         std::string newOwnerPeerId);
  [[nodiscard]] OwnershipResult applyAuthoritativeTransfer(
      std::string_view networkId, std::string newOwnerPeerId,
      std::uint64_t epoch, std::uint64_t generation);
  [[nodiscard]] OwnershipResult despawn(std::string_view networkId);
  [[nodiscard]] OwnershipResult applyAuthoritativeDespawn(
      std::string_view networkId, std::uint64_t epoch,
      std::uint64_t generation);
  [[nodiscard]] DisconnectOwnershipActions
  disconnectPeer(const NetworkContract &contract, std::string_view peerId);

  [[nodiscard]] const NetworkOwnedEntity *find(std::string_view networkId) const;
  [[nodiscard]] bool isOwner(std::string_view networkId,
                             std::string_view peerId) const;
  [[nodiscard]] std::vector<NetworkOwnedEntity> snapshot() const;

private:
  [[nodiscard]] OwnershipResult reject(OwnershipRejectCode code,
                                       std::string reason) const;
  [[nodiscard]] std::size_t ownedCount(std::string_view peerId) const;

  bool authoritativeServer_ = false;
  std::uint64_t sessionEpoch_ = 0;
  std::uint64_t nextNetworkId_ = 1;
  std::unordered_map<std::string, NetworkOwnedEntity> entities_;
};

[[nodiscard]] std::string_view ownershipRejectCodeName(OwnershipRejectCode code);

} // namespace demi::runtime
