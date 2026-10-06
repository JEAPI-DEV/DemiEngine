#include "demi/runtime/terrain/TerrainScatterRuntime.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime {
namespace {

// The prefix every native scattered child carries, so an instance can be found
// and released without consulting the generator again.
constexpr std::string_view scatterSegment = "/__scatter/";

std::string instancePrefix(std::string_view ownerId,
                           std::string_view paletteId) {
  std::string prefix{ownerId};
  prefix += scatterSegment;
  prefix += paletteId;
  prefix += "/";
  return prefix;
}

void applyTransform(Entity &entity, const TerrainScatterPlacement &placement,
                    std::string_view ownerId) {
  auto *transform = entity.component<Transform3DComponent>();
  if (transform == nullptr)
    return;
  transform->parent = ownerId;
  transform->position = placement.position;
  transform->rotation = {0, placement.yaw, 0};
  transform->scale = {placement.scale, placement.scale, placement.scale};
  entity.serializedComponents[std::string(Transform3DComponent::typeName)] =
      nlohmann::json({{"parent", transform->parent},
                      {"position", {transform->position.x,
                                    transform->position.y,
                                    transform->position.z}},
                      {"rotation", {transform->rotation.x,
                                    transform->rotation.y,
                                    transform->rotation.z}},
                      {"scale", {transform->scale.x, transform->scale.y,
                                 transform->scale.z}}})
          .dump();
}

void applyRenderer(Entity &entity, const TerrainScatterPlacement &placement) {
  // MeshRenderer expresses LOD as model tiers rather than a level, so the
  // palette's lod level is not fabricated into a field that means something
  // else. It travels in the prefab override payload instead, where it stays
  // data for the consumer that understands it.
  auto *renderer = entity.component<MeshRendererComponent>();
  if (renderer == nullptr)
    return;
  renderer->model = placement.asset;
}

bool sameInstance(const Entity &entity, const TerrainScatterPlacement &placement,
                  std::string_view ownerId) {
  const auto *transform = entity.component<Transform3DComponent>();
  if (transform == nullptr)
    return false;
  if (transform->parent != ownerId)
    return false;
  if (transform->position.x != placement.position.x ||
      transform->position.y != placement.position.y ||
      transform->position.z != placement.position.z)
    return false;
  if (transform->rotation.y != placement.yaw)
    return false;
  if (transform->scale.x != placement.scale)
    return false;
  if (!placement.prefab.empty())
    return !entity.prefabInstance.empty();
  const auto *renderer = entity.component<MeshRendererComponent>();
  if (renderer == nullptr)
    return false;
  return renderer->model == placement.asset;
}

} // namespace

std::string terrainScatterInstanceId(std::string_view ownerId,
                                     std::string_view paletteId,
                                     TerrainPaletteRole role, std::size_t cell) {
  return instancePrefix(ownerId, paletteId) +
         std::string(terrainPaletteRoleName(role)) + "_" + std::to_string(cell);
}

TerrainScatterSyncStats
syncTerrainScatter(World &world, WorldCommandBuffer &commands,
                   std::string_view ownerId, const HeightField &field,
                   const TerrainPrefabServiceProvider &prefabProvider,
                   std::string &error) {
  TerrainScatterSyncStats stats;
  error.clear();
  const auto *owner = findEntity(world, std::string(ownerId));
  if (owner == nullptr) {
    error = "Terrain scatter owner was not found: " + std::string(ownerId);
    return stats;
  }

  const auto prefix = instancePrefix(ownerId, field.paletteId);

  // Existing instances, indexed by the stable key, so a placement that did not
  // change can be left alone and one that moved can be updated in place.
  std::unordered_map<std::string, std::string> existing;
  for (const auto &entity : world.entities) {
    if (entity.id.rfind(prefix, 0) != 0)
      continue;
    const auto *transform = entity.component<Transform3DComponent>();
    if (entity.enabled && (entity.prefabInstance.empty() ||
                           (transform != nullptr &&
                            (transform->parent.empty() || transform->parent == ownerId))))
      existing.emplace(entity.prefabInstance.empty() ? entity.id
                                                      : entity.prefabInstance,
                       entity.id);
  }

  std::unordered_set<std::string> wanted;
  wanted.reserve(field.scatterPlacements.size());
  for (const auto &placement : field.scatterPlacements) {
    const auto id =
        terrainScatterInstanceId(ownerId, field.paletteId, placement.role,
                                 placement.cell);
    wanted.insert(id);

    const auto found = existing.find(id);
    if (found != existing.end()) {
      const auto *current = findEntity(world, found->second);
      if (current == nullptr) {
        error = "Terrain scatter instance vanished before update: " + id;
        return stats;
      }
      if (current->prefabInstance.empty() != placement.prefab.empty()) {
        error = "Terrain scatter instance kind changed for " + id +
                "; rebuild the palette placement.";
        return stats;
      }
      if (sameInstance(*current, placement, ownerId)) {
        ++stats.retained;
        continue;
      }
      // Same cell, different transform or asset: move the instance so prefab
      // state survives a sculpt.
      Entity updated = *current;
      applyTransform(updated, placement, ownerId);
      if (placement.prefab.empty())
        applyRenderer(updated, placement);
      if (!commands.create(world, std::move(updated), true)) {
        error = "Terrain scatter could not update " + id;
        return stats;
      }
      ++stats.updated;
      continue;
    }

    if (placement.prefab.empty()) {
      // No prefab to instantiate, so a plain entity carries the transform and
      // the mesh reference. The palette decides which this is.
      Entity instance;
      instance.id = id;
      instance.name = "scatter_" + std::string(terrainPaletteRoleName(placement.role));
      instance.layer = owner->layer;
      instance.enabled = true;
      Entity staged = instance;
      staged.setComponent(Transform3DComponent{});
      staged.setComponent(MeshRendererComponent{});
      applyTransform(staged, placement, ownerId);
      applyRenderer(staged, placement);
      if (!commands.create(world, std::move(staged))) {
        error = "Terrain scatter could not create " + id;
        return stats;
      }
      ++stats.created;
      continue;
    }

    auto *prefabs = prefabProvider ? prefabProvider() : nullptr;
    if (prefabs == nullptr) {
      ++stats.unresolved;
      continue;
    }
    PrefabInstantiateOptions options;
    options.id = id;
    options.position = placement.position;
    // A placement's cell ID must remain its instance ID. Generic pool reuse
    // may select an available instance with another cell's ID.
    options.pooled = false;
    auto instance = prefabs->instantiate(world, commands, placement.prefab, options);
    if (!instance) {
      if (error.empty())
        error = "Terrain scatter could not instantiate " + placement.prefab +
                " for " + id + ": " +
                (instance.diagnostics.empty() ? "unknown prefab error"
                                              : instance.diagnostics.front().message);
      ++stats.unresolved;
      continue;
    }
    for (const auto &entityId : instance.entityIds) {
      Entity *staged = commands.pendingEntity(entityId);
      auto *transform = staged == nullptr
                            ? nullptr
                            : staged->component<Transform3DComponent>();
      if (transform != nullptr && transform->parent.empty()) {
        applyTransform(*staged, placement, ownerId);
      }
    }
    ++stats.created;
    ++stats.prefabInstances;
  }

  // Anything the previous field produced that this one did not must go, or the
  // terrain would accumulate instances every stroke.
  std::unordered_set<std::string> released;
  for (const auto &entity : world.entities) {
    if (entity.id.rfind(prefix, 0) != 0)
      continue;
    const std::string &key = entity.prefabInstance.empty()
                                 ? entity.id
                                 : entity.prefabInstance;
    if (wanted.contains(key) || !entity.enabled)
      continue;
    // Prefab instances are released through their service; direct mesh
    // entities have no service state.
    if (!entity.prefabInstance.empty()) {
      if (!released.insert(entity.prefabInstance).second)
        continue;
      auto *prefabs = prefabProvider ? prefabProvider() : nullptr;
      if (!prefabs) {
        error = "Terrain scatter requires a prefab service to release " + entity.prefabInstance;
        return stats;
      }
      if (!prefabs->release(world, commands, entity.prefabInstance)) {
        error = "Terrain scatter could not release " + entity.prefabInstance;
        return stats;
      }
      ++stats.removed;
      continue;
    }
    if (!commands.destroy(world, entity.id)) {
      error = "Terrain scatter could not remove " + entity.id;
      return stats;
    }
    ++stats.removed;
  }
  return stats;
}

TerrainScatterSyncStats
syncTerrainScatter(World &world, WorldCommandBuffer &commands,
                   std::string_view ownerId, const HeightField &field,
                   RuntimePrefabService *prefabs, std::string &error) {
  return syncTerrainScatter(world, commands, ownerId, field,
                            TerrainPrefabServiceProvider{[prefabs] { return prefabs; }}, error);
}

std::size_t releaseTerrainScatter(World &world, WorldCommandBuffer &commands,
                                  std::string_view ownerId) {
  std::string prefix{ownerId};
  prefix += scatterSegment;
  std::vector<std::string> doomed;
  for (const auto &entity : world.entities)
    if (entity.id.rfind(prefix, 0) == 0)
      doomed.push_back(entity.id);
  for (const auto &id : doomed)
    if (!commands.destroy(world, id))
      return 0;
  return doomed.size();
}

} // namespace demi::runtime
