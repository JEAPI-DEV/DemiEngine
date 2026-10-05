#include "demi/runtime/terrain/TerrainWorldBatch.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ModelCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/terrain/TerrainMeshBuilder.h"
#include "demi/runtime/terrain/TerrainScatterRuntime.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainWorld.h"

#include <algorithm>
#include <exception>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime {
namespace {
struct OwnerPublication {
  std::size_t index;
  Terrain3DComponent terrain;
  std::unordered_map<std::string, std::string>::node_type serialized;
};

struct AppearancePublication {
  std::size_t index;
  Color color;
  std::string material;
  std::string texture;
};

Entity ownerSnapshot(const Entity &owner) {
  Entity snapshot;
  snapshot.id = owner.id;
  snapshot.enabled = owner.enabled;
  snapshot.layer = owner.layer;
  snapshot.sceneOwner = owner.sceneOwner;
  snapshot.prefabInstance = owner.prefabInstance;
  snapshot.persistent = owner.persistent;
  if (const auto *transform = owner.component<Transform3DComponent>())
    snapshot.setComponent(*transform);
  snapshot.setComponent(*owner.component<Terrain3DComponent>());
  const auto serialized = owner.serializedComponents.find("Terrain3D");
  if (serialized != owner.serializedComponents.end())
    snapshot.serializedComponents.emplace(*serialized);
  return snapshot;
}

Entity appearanceSnapshot(const Entity &surface) {
  Entity snapshot;
  snapshot.id = surface.id;
  snapshot.enabled = surface.enabled;
  if (const auto *mesh = surface.component<MeshRendererComponent>()) {
    MeshRendererComponent appearance;
    appearance.color = mesh->color;
    appearance.material = mesh->material;
    appearance.texture = mesh->texture;
    appearance.revision = mesh->revision;
    snapshot.setComponent(std::move(appearance));
  }
  if (const auto *collider = surface.component<ModelCollider3DComponent>())
    snapshot.setComponent(*collider);
  snapshot.setComponent(
      *surface.component<terrain_detail::TerrainGeneratedSurface>());
  return snapshot;
}
} // namespace

namespace {

// Coordinates an atomic native terrain/scenery publication. Geometry creation,
// scatter policy and entity commands remain with their existing subsystem
// owners.
class TerrainWorldPublication {
public:
  TerrainWorldPublication(World &worldValue,
                          std::span<const std::string> ownerIds,
                          const nlohmann::json &recipeValue,
                          const TerrainUpdate &updateValue,
                          RuntimePrefabService *prefabService)
      : world(worldValue), recipe(recipeValue), native(updateValue),
        prefabs(prefabService) {
    const std::unordered_set<std::string> wantedOwners(ownerIds.begin(),
                                                       ownerIds.end());
    for (std::size_t index = 0; index < world.entities.size(); ++index) {
      const auto &entity = world.entities[index];
      if (!indices.emplace(entity.id, index).second)
        throw std::invalid_argument("Duplicate entity ID: " + entity.id);
      if (const auto owner = terrainSurfaceOwner(entity))
        surfaces[std::string(*owner)].push_back(index);
      const auto *terrain = entity.component<Terrain3DComponent>();
      if (terrain && wantedOwners.contains(entity.id))
        owners.push_back(index);
    }
    if (owners.size() != wantedOwners.size())
      throw std::invalid_argument("A terrain publication owner is missing or "
                                  "has no Terrain3D component.");
  }

  void prepareScatter(std::string &error) {
    // Prefab instantiation/release changes service bookkeeping while queuing
    // commands. Keep that bookkeeping private until geometry and scenery pass.
    const TerrainPrefabServiceProvider prefabProvider =
        [&]() -> RuntimePrefabService * {
      if (!prefabs)
        return nullptr;
      if (!stagedPrefabs) {
        stagedPrefabs = *prefabs;
        stagedPrefabs->prune(world);
      }
      return &*stagedPrefabs;
    };
    WorldCommandBuffer scatterCommands;
    for (const auto ownerIndex : owners) {
      const auto &owner = world.entities[ownerIndex];
      const auto *terrain = owner.component<Terrain3DComponent>();
      if (terrain->generated &&
          terrain->generated->paletteId != native.field->paletteId) {
        const std::string prefix = owner.id + "/__scatter/";
        std::unordered_set<std::string> released;
        for (const auto &entity : world.entities) {
          if (!entity.id.starts_with(prefix))
            continue;
          if (!entity.prefabInstance.empty()) {
            if (!released.insert(entity.prefabInstance).second)
              continue;
            auto *service = prefabProvider();
            if (!service || !service->release(world, scatterCommands,
                                              entity.prefabInstance))
              throw std::invalid_argument("Terrain scatter could not release " +
                                          entity.prefabInstance);
          } else if (!scatterCommands.destroy(world, entity.id)) {
            throw std::invalid_argument("Terrain scatter could not remove " +
                                        entity.id);
          }
        }
      }
      const auto stats =
          syncTerrainScatter(world, scatterCommands, owner.id, *native.field,
                             prefabProvider, error);
      if (!error.empty())
        throw std::invalid_argument(error);
      if (stats.unresolved != 0)
        throw std::invalid_argument(
            "Terrain scatter requires a configured prefab service on " +
            owner.id);
    }
    // Queueing is read-only for the world. Flush into a snapshot of only IDs
    // actually mentioned by commands, preserving arbitrary live prefab state.
    World scatterPreview;
    scatterPreview.activeSceneId = world.activeSceneId;
    const auto scatterIds = scatterCommands.affectedEntityIds();
    std::unordered_set<std::size_t> scatterTouched;
    for (const auto &id : scatterIds) {
      const auto found = indices.find(id);
      if (found != indices.end()) {
        scatterTouched.insert(found->second);
        scatterPreview.entities.push_back(world.entities[found->second]);
      }
    }
    (void)scatterCommands.flush(scatterPreview);
    for (auto &entity : scatterPreview.entities) {
      const auto existing = indices.find(entity.id);
      if (existing != indices.end()) {
        scatterTouched.erase(existing->second);
        replacements.emplace(existing->second, std::move(entity));
      } else {
        additionIds.insert(entity.id);
        additions.push_back(std::move(entity));
      }
    }
    removals.insert(scatterTouched.begin(), scatterTouched.end());
  }

  void prepareGeometry(std::string &error) {
    for (const auto ownerIndex : owners) {
      const auto &owner = world.entities[ownerIndex];
      const auto *terrain = owner.component<Terrain3DComponent>();
      if (terrain->generated == native.field && terrain->recipe == recipe)
        continue;
      World staged;
      staged.entities.push_back(ownerSnapshot(owner));

      // Native visibility follows the authored parent chain. Ancestors need
      // only identity, enabled state and transform in this publication
      // snapshot.
      const auto *ancestor = &owner;
      std::unordered_set<std::string> visited{owner.id};
      while (const auto *transform =
                 ancestor->component<Transform3DComponent>()) {
        if (transform->parent.empty())
          break;
        const auto found = indices.find(transform->parent);
        if (found == indices.end() || !visited.insert(transform->parent).second)
          throw std::invalid_argument("Terrain has an invalid parent chain: " +
                                      owner.id);
        ancestor = &world.entities[found->second];
        Entity context;
        context.id = ancestor->id;
        context.enabled = ancestor->enabled;
        if (const auto *parentTransform =
                ancestor->component<Transform3DComponent>())
          context.setComponent(*parentTransform);
        staged.entities.push_back(std::move(context));
      }

      const bool allChunks = native.invalidation.fullGeneration ||
                             native.invalidation.layoutChanged ||
                             native.invalidation.materialsChanged;
      const auto dirty = native.invalidation.geometrySamples();
      const bool appearanceOnly = dirty.empty() &&
                                  !native.invalidation.fullGeneration &&
                                  !native.invalidation.layoutChanged;
      std::map<std::pair<int, int>, TerrainRect> retainedChunks;
      if (!allChunks && terrain->generated) {
        for (const auto &chunk : terrain->generated->chunks)
          retainedChunks.emplace(std::pair{chunk.firstCellX, chunk.firstCellZ},
                                 TerrainRect{chunk.firstCellX, chunk.firstCellZ,
                                             chunk.firstCellX + chunk.cellsX,
                                             chunk.firstCellZ + chunk.cellsZ});
      }
      std::unordered_set<std::size_t> touched;
      for (const auto index : surfaces[owner.id]) {
        const auto &entity = world.entities[index];
        const auto *surface =
            entity.component<terrain_detail::TerrainGeneratedSurface>();
        if (!allChunks) {
          const auto chunk =
              retainedChunks.find({surface->firstCellX, surface->firstCellZ});
          if (chunk == retainedChunks.end())
            throw std::invalid_argument(
                "Terrain surface has no retained chunk: " + entity.id);
          if (!dirty.intersects(chunk->second))
            continue;
        }
        touched.insert(index);
        staged.entities.push_back(appearanceOnly ? appearanceSnapshot(entity)
                                                 : entity);
      }
      auto prepared =
          prepareTerrainWorldUpdate(staged, owner.id, recipe, native, error);
      if (!prepared ||
          !publishTerrainWorldUpdate(staged, std::move(*prepared), error))
        throw std::invalid_argument(error);
      for (auto &entity : staged.entities) {
        if (entity.id != owner.id && terrainSurfaceOwner(entity) != owner.id)
          continue;
        const auto existing = indices.find(entity.id);
        if (existing != indices.end()) {
          if (existing->second != ownerIndex &&
              !touched.contains(existing->second))
            throw std::invalid_argument(
                "Generated terrain entity ID conflicts with existing entity: " +
                entity.id);
          touched.erase(existing->second);
          if (existing->second == ownerIndex) {
            ownerPublications.push_back(
                {ownerIndex, std::move(*entity.component<Terrain3DComponent>()),
                 entity.serializedComponents.extract("Terrain3D")});
            continue;
          }
          const auto *previousMesh = world.entities[existing->second]
                                         .component<MeshRendererComponent>();
          const auto *previousCollider =
              world.entities[existing->second]
                  .component<ModelCollider3DComponent>();
          const auto *mesh = entity.component<MeshRendererComponent>();
          const auto *collider = entity.component<ModelCollider3DComponent>();
          if (mesh && previousMesh && collider && previousCollider &&
              mesh->revision == previousMesh->revision &&
              collider->inlineGeometry == previousCollider->inlineGeometry) {
            appearances.push_back(
                {existing->second, mesh->color, mesh->material, mesh->texture});
          } else {
            replacements.emplace(existing->second, std::move(entity));
          }
        } else {
          if (!additionIds.insert(entity.id).second)
            throw std::invalid_argument(
                "Duplicate generated terrain entity ID: " + entity.id);
          additions.push_back(std::move(entity));
        }
      }
      removals.insert(touched.begin(), touched.end());
    }
  }

  void prepareOwnership() {
    ownershipChanged =
        std::ranges::any_of(additions,
                            [](const auto &entity) {
                              return terrainSurfaceOwner(entity).has_value();
                            }) ||
        std::ranges::any_of(removals, [&](const auto index) {
          return terrainSurfaceOwner(world.entities[index]).has_value();
        });
    if (!ownershipChanged)
      return;
    nextOwners = world.terrainOwners;
    for (const auto index : owners) {
      const auto &ownerId = world.entities[index].id;
      auto found =
          std::ranges::find(nextOwners, ownerId, &TerrainRuntimeOwner::id);
      if (found == nextOwners.end()) {
        nextOwners.push_back({.id = ownerId});
        found = std::prev(nextOwners.end());
      }
      found->surfaces.clear();
      for (const auto surfaceIndex : surfaces[ownerId])
        if (!removals.contains(surfaceIndex))
          found->surfaces.push_back(world.entities[surfaceIndex].id);
      for (const auto &entity : additions)
        if (terrainSurfaceOwner(entity) == ownerId)
          found->surfaces.push_back(entity.id);
    }
  }

  void commit() {
    // Reserve before changing any live entity. Entity moves and container swaps
    // below cannot fail; a bad later placement leaves every earlier one intact.
    if (additions.size() > world.entities.max_size() - world.entities.size())
      throw std::length_error(
          "Generated terrain exceeds entity storage capacity.");
    world.entities.reserve(world.entities.size() + additions.size());
    for (const auto &publication : ownerPublications) {
      auto &components = world.entities[publication.index].serializedComponents;
      if (!components.contains("Terrain3D"))
        components.reserve(components.size() + 1);
    }
    static_assert(std::is_nothrow_move_assignable_v<Entity>);
    static_assert(std::is_nothrow_move_constructible_v<Entity>);
    for (auto &publication : ownerPublications) {
      auto &owner = world.entities[publication.index];
      auto *terrain = owner.component<Terrain3DComponent>();
      terrain->recipe.swap(publication.terrain.recipe);
      terrain->generated.swap(publication.terrain.generated);
      const auto previous = owner.serializedComponents.find("Terrain3D");
      if (previous != owner.serializedComponents.end())
        previous->second.swap(publication.serialized.mapped());
      else
        owner.serializedComponents.insert(std::move(publication.serialized));
    }
    for (auto &appearance : appearances) {
      auto *mesh =
          world.entities[appearance.index].component<MeshRendererComponent>();
      mesh->color = appearance.color;
      mesh->material.swap(appearance.material);
      mesh->texture.swap(appearance.texture);
    }
    for (auto &[index, entity] : replacements)
      world.entities[index] = std::move(entity);
    std::size_t index = 0;
    std::erase_if(world.entities,
                  [&](const auto &) { return removals.contains(index++); });
    for (auto &entity : additions)
      world.entities.push_back(std::move(entity));
    if (ownershipChanged) {
      world.terrainOwners.swap(nextOwners);
    }
    if (!removals.empty() || !additions.empty())
      world.terrainEntityLookup.clear();
    if (prefabs && stagedPrefabs) {
      static_assert(std::is_nothrow_move_assignable_v<RuntimePrefabService>);
      *prefabs = std::move(*stagedPrefabs);
    }
  }

private:
  World &world;
  const nlohmann::json &recipe;
  const TerrainUpdate &native;
  RuntimePrefabService *prefabs;
  std::unordered_map<std::string, std::size_t> indices;
  std::unordered_map<std::string, std::vector<std::size_t>> surfaces;
  std::vector<std::size_t> owners;
  std::unordered_map<std::size_t, Entity> replacements;
  std::unordered_set<std::size_t> removals;
  std::vector<Entity> additions;
  std::unordered_set<std::string> additionIds;
  std::vector<OwnerPublication> ownerPublications;
  std::vector<AppearancePublication> appearances;
  std::optional<RuntimePrefabService> stagedPrefabs;
  std::vector<TerrainRuntimeOwner> nextOwners;
  bool ownershipChanged = false;
};

} // namespace

bool updateTerrainWorldBatch(World &world,
                             std::span<const std::string> ownerIds,
                             const nlohmann::json &recipe,
                             const TerrainUpdate &update, std::string &error,
                             RuntimePrefabService *prefabs) {
  error.clear();
  try {
    if (!update.field || !update.patch)
      throw std::invalid_argument(
          "Terrain publication requires a field and patch.");
    TerrainWorldPublication publication(world, ownerIds, recipe, update,
                                        prefabs);
    publication.prepareScatter(error);
    publication.prepareGeometry(error);
    publication.prepareOwnership();
    publication.commit();
    return true;
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }
}

bool updateTerrainAssetWorld(World &world, std::string_view assetId,
                             const nlohmann::json &recipe,
                             const TerrainUpdate &update, std::string &error,
                             RuntimePrefabService *prefabs) {
  if (!assetId.starts_with("asset://") ||
      assetId.size() == std::string_view("asset://").size()) {
    error =
        "Shared terrain publication requires a nonempty asset:// reference.";
    return false;
  }
  std::vector<std::string> owners;
  for (const auto &entity : world.entities) {
    const auto *terrain = entity.component<Terrain3DComponent>();
    if (terrain && terrain->asset == assetId)
      owners.push_back(entity.id);
  }
  return updateTerrainWorldBatch(world, owners, recipe, update, error, prefabs);
}

} // namespace demi::runtime
