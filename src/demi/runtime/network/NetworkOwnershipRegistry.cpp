#include "demi/runtime/network/NetworkOwnershipRegistry.h"

#include <algorithm>
#include <limits>

namespace demi::runtime {

NetworkOwnershipRegistry::NetworkOwnershipRegistry(
    const bool authoritativeServer) {
  reset(authoritativeServer);
}

void NetworkOwnershipRegistry::reset(const bool authoritativeServer) {
  authoritativeServer_ = authoritativeServer;
  sessionEpoch_ = sessionEpoch_ == std::numeric_limits<std::uint64_t>::max()
                      ? 1
                      : sessionEpoch_ + 1;
  nextNetworkId_ = 1;
  entities_.clear();
}

bool NetworkOwnershipRegistry::synchronizeEpoch(
    const std::uint64_t authoritativeEpoch) {
  if (authoritativeServer_ || authoritativeEpoch == 0)
    return false;
  sessionEpoch_ = authoritativeEpoch;
  nextNetworkId_ = 1;
  entities_.clear();
  return true;
}

bool NetworkOwnershipRegistry::authoritativeServer() const {
  return authoritativeServer_;
}

std::uint64_t NetworkOwnershipRegistry::sessionEpoch() const {
  return sessionEpoch_;
}

std::size_t NetworkOwnershipRegistry::size() const { return entities_.size(); }

OwnershipResult
NetworkOwnershipRegistry::spawn(const NetworkContract &contract,
                                const std::string_view prefabKey,
                                std::string ownerPeerId) {
  if (!authoritativeServer_)
    return reject(OwnershipRejectCode::NotServer,
                  "only the authoritative server may spawn network entities");
  const auto prefab = contract.replicatedPrefabs.find(std::string(prefabKey));
  if (prefab == contract.replicatedPrefabs.end())
    return reject(OwnershipRejectCode::UnknownPrefab,
                  "unknown replicated prefab");
  if (ownerPeerId.empty())
    return reject(OwnershipRejectCode::InvalidOwner, "owner is required");
  if (ownerPeerId != "server" &&
      ownedCount(ownerPeerId) >= contract.limits.maximumOwnedEntitiesPerPeer)
    return reject(OwnershipRejectCode::EntityLimit,
                  "peer reached the owned entity limit");
  const std::string id = "net:" + std::to_string(sessionEpoch_) + ":" +
                         std::to_string(nextNetworkId_++);
  NetworkOwnedEntity entity{.networkId = id,
                            .prefabKey = std::string(prefabKey),
                            .ownerPeerId = std::move(ownerPeerId),
                            .sessionEpoch = sessionEpoch_,
                            .ownershipGeneration = 1};
  entities_.emplace(id, entity);
  return {.accepted = true,
          .code = OwnershipRejectCode::None,
          .reason = {},
          .entity = std::move(entity)};
}

OwnershipResult
NetworkOwnershipRegistry::applyAuthoritativeSpawn(NetworkOwnedEntity entity) {
  if (authoritativeServer_)
    return reject(OwnershipRejectCode::NotServer,
                  "server cannot apply a remote authoritative spawn");
  if (entity.networkId.empty() || entity.ownerPeerId.empty())
    return reject(OwnershipRejectCode::InvalidOwner,
                  "authoritative spawn is missing identity");
  if (entity.sessionEpoch != sessionEpoch_)
    return reject(OwnershipRejectCode::StaleEpoch,
                  "spawn belongs to another session epoch");
  if (entities_.contains(entity.networkId))
    return reject(OwnershipRejectCode::DuplicateEntity,
                  "network entity already exists");
  entities_.emplace(entity.networkId, entity);
  return {.accepted = true,
          .code = OwnershipRejectCode::None,
          .reason = {},
          .entity = std::move(entity)};
}

OwnershipResult
NetworkOwnershipRegistry::transfer(const NetworkContract &contract,
                                   const std::string_view networkId,
                                   std::string newOwnerPeerId) {
  if (!authoritativeServer_)
    return reject(OwnershipRejectCode::NotServer,
                  "only the authoritative server may transfer ownership");
  auto found = entities_.find(std::string(networkId));
  if (found == entities_.end())
    return reject(OwnershipRejectCode::UnknownEntity,
                  "network entity does not exist");
  if (newOwnerPeerId.empty())
    return reject(OwnershipRejectCode::InvalidOwner, "owner is required");
  if (newOwnerPeerId != "server" &&
      newOwnerPeerId != found->second.ownerPeerId &&
      ownedCount(newOwnerPeerId) >= contract.limits.maximumOwnedEntitiesPerPeer)
    return reject(OwnershipRejectCode::EntityLimit,
                  "peer reached the owned entity limit");
  found->second.ownerPeerId = std::move(newOwnerPeerId);
  ++found->second.ownershipGeneration;
  return {.accepted = true,
          .code = OwnershipRejectCode::None,
          .reason = {},
          .entity = found->second};
}

OwnershipResult NetworkOwnershipRegistry::applyAuthoritativeTransfer(
    const std::string_view networkId, std::string newOwnerPeerId,
    const std::uint64_t epoch, const std::uint64_t generation) {
  if (authoritativeServer_)
    return reject(OwnershipRejectCode::NotServer,
                  "server cannot apply a remote authoritative transfer");
  auto found = entities_.find(std::string(networkId));
  if (found == entities_.end())
    return reject(OwnershipRejectCode::UnknownEntity,
                  "network entity does not exist");
  if (epoch != sessionEpoch_)
    return reject(OwnershipRejectCode::StaleEpoch,
                  "transfer belongs to another session epoch");
  if (generation <= found->second.ownershipGeneration)
    return reject(OwnershipRejectCode::StaleGeneration,
                  "ownership transfer is stale or duplicated");
  found->second.ownerPeerId = std::move(newOwnerPeerId);
  found->second.ownershipGeneration = generation;
  return {.accepted = true,
          .code = OwnershipRejectCode::None,
          .reason = {},
          .entity = found->second};
}

OwnershipResult
NetworkOwnershipRegistry::despawn(const std::string_view networkId) {
  if (!authoritativeServer_)
    return reject(OwnershipRejectCode::NotServer,
                  "only the authoritative server may despawn network entities");
  auto found = entities_.find(std::string(networkId));
  if (found == entities_.end())
    return reject(OwnershipRejectCode::UnknownEntity,
                  "network entity does not exist");
  NetworkOwnedEntity entity = found->second;
  ++entity.ownershipGeneration;
  entities_.erase(found);
  return {.accepted = true,
          .code = OwnershipRejectCode::None,
          .reason = {},
          .entity = std::move(entity)};
}

OwnershipResult NetworkOwnershipRegistry::applyAuthoritativeDespawn(
    const std::string_view networkId, const std::uint64_t epoch,
    const std::uint64_t generation) {
  if (authoritativeServer_)
    return reject(OwnershipRejectCode::NotServer,
                  "server cannot apply a remote authoritative despawn");
  auto found = entities_.find(std::string(networkId));
  if (found == entities_.end())
    return reject(OwnershipRejectCode::UnknownEntity,
                  "network entity does not exist");
  if (epoch != sessionEpoch_)
    return reject(OwnershipRejectCode::StaleEpoch,
                  "despawn belongs to another session epoch");
  if (generation <= found->second.ownershipGeneration)
    return reject(OwnershipRejectCode::StaleGeneration,
                  "despawn is stale or duplicated");
  NetworkOwnedEntity entity = found->second;
  entity.ownershipGeneration = generation;
  entities_.erase(found);
  return {.accepted = true,
          .code = OwnershipRejectCode::None,
          .reason = {},
          .entity = std::move(entity)};
}

DisconnectOwnershipActions
NetworkOwnershipRegistry::disconnectPeer(const NetworkContract &contract,
                                         const std::string_view peerId) {
  DisconnectOwnershipActions actions;
  if (!authoritativeServer_)
    return actions;
  for (auto iterator = entities_.begin(); iterator != entities_.end();) {
    if (iterator->second.ownerPeerId != peerId) {
      ++iterator;
      continue;
    }
    const auto prefab =
        contract.replicatedPrefabs.find(iterator->second.prefabKey);
    const auto policy = prefab == contract.replicatedPrefabs.end()
                            ? NetworkDisconnectPolicy::Despawn
                            : prefab->second.onDisconnect;
    if (policy == NetworkDisconnectPolicy::Despawn) {
      NetworkOwnedEntity entity = iterator->second;
      ++entity.ownershipGeneration;
      actions.despawned.push_back(std::move(entity));
      iterator = entities_.erase(iterator);
    } else if (policy == NetworkDisconnectPolicy::ReturnToServer) {
      iterator->second.ownerPeerId = "server";
      ++iterator->second.ownershipGeneration;
      actions.returnedToServer.push_back(iterator->second);
      ++iterator;
    } else {
      // Revoke first. Game policy may grant a new owner in a later command.
      iterator->second.ownerPeerId = "server";
      ++iterator->second.ownershipGeneration;
      actions.awaitingGamePolicy.push_back(iterator->second);
      ++iterator;
    }
  }
  return actions;
}

const NetworkOwnedEntity *
NetworkOwnershipRegistry::find(const std::string_view networkId) const {
  const auto found = entities_.find(std::string(networkId));
  return found == entities_.end() ? nullptr : &found->second;
}

bool NetworkOwnershipRegistry::isOwner(const std::string_view networkId,
                                       const std::string_view peerId) const {
  const NetworkOwnedEntity *entity = find(networkId);
  return entity != nullptr && entity->ownerPeerId == peerId;
}

std::vector<NetworkOwnedEntity> NetworkOwnershipRegistry::snapshot() const {
  std::vector<NetworkOwnedEntity> result;
  result.reserve(entities_.size());
  for (const auto &[unused, entity] : entities_) {
    (void)unused;
    result.push_back(entity);
  }
  std::ranges::sort(result, {}, &NetworkOwnedEntity::networkId);
  return result;
}

OwnershipResult NetworkOwnershipRegistry::reject(const OwnershipRejectCode code,
                                                 std::string reason) const {
  return {.accepted = false,
          .code = code,
          .reason = std::move(reason),
          .entity = std::nullopt};
}

std::size_t
NetworkOwnershipRegistry::ownedCount(const std::string_view peerId) const {
  return static_cast<std::size_t>(
      std::ranges::count_if(entities_, [&](const auto &entry) {
        return entry.second.ownerPeerId == peerId;
      }));
}

std::string_view ownershipRejectCodeName(const OwnershipRejectCode code) {
  switch (code) {
  case OwnershipRejectCode::None:
    return "none";
  case OwnershipRejectCode::NotServer:
    return "not_server";
  case OwnershipRejectCode::UnknownPrefab:
    return "unknown_prefab";
  case OwnershipRejectCode::UnknownEntity:
    return "unknown_entity";
  case OwnershipRejectCode::DuplicateEntity:
    return "duplicate_entity";
  case OwnershipRejectCode::InvalidOwner:
    return "invalid_owner";
  case OwnershipRejectCode::EntityLimit:
    return "entity_limit";
  case OwnershipRejectCode::StaleEpoch:
    return "stale_epoch";
  case OwnershipRejectCode::StaleGeneration:
    return "stale_generation";
  }
  return "unknown";
}

} // namespace demi::runtime
