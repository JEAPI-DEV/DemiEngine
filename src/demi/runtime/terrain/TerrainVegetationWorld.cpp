#include "demi/runtime/terrain/TerrainVegetationWorld.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/terrain/TerrainCellPreparation.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/terrain/TerrainScatterRuntime.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <tuple>
#include <stdexcept>

namespace demi::runtime {
namespace {
bool samePosition(Vec3 a, Vec3 b) {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

// Prefab operations mutate bookkeeping while staging entity commands. Keep a
// private copy until all source validation succeeds, just like terrain edits.
class CellPublication {
public:
  CellPublication(World &world, RuntimePrefabService *prefabs)
      : world_(world), prefabs_(prefabs) {}

  RuntimePrefabService *prefabs() {
    if (!prefabs_) {
      return nullptr;
    }
    if (!stagedPrefabs_) {
      stagedPrefabs_ = *prefabs_;
      stagedPrefabs_->prune(world_);
    }
    return &*stagedPrefabs_;
  }

  void remove(std::string_view cellId) {
    const std::string prefix = std::string(cellId) + "/__scatter/";
    std::set<std::string> released;
    for (const auto &entity : world_.entities) {
      if (!entity.id.starts_with(prefix)) {
        continue;
      }
      if (!entity.prefabInstance.empty()) {
        if (!released.insert(entity.prefabInstance).second) {
          continue;
        }
        auto *service = prefabs();
        if (!service ||
            !service->release(world_, commands, entity.prefabInstance)) {
          throw std::runtime_error("Cannot release vegetation prefab " +
                                   entity.prefabInstance);
        }
      } else if (!commands.destroy(world_, entity.id)) {
        throw std::runtime_error("Cannot remove vegetation entity " +
                                 entity.id);
      }
    }
    if (!commands.destroy(world_, std::string(cellId))) {
      throw std::runtime_error("Cannot remove vegetation cell " +
                               std::string(cellId));
    }
  }

  void commit() {
    ProfileScope profile("Terrain.cell_commit");
    (void)commands.flush(world_);
    if (stagedPrefabs_) {
      *prefabs_ = std::move(*stagedPrefabs_);
    }
  }

  WorldCommandBuffer commands;

private:
  World &world_;
  RuntimePrefabService *prefabs_;
  std::optional<RuntimePrefabService> stagedPrefabs_;
};
} // namespace

std::string terrainVegetationCellOwnerId(std::string_view ownerId,
                                         std::int64_t x, std::int64_t z,
                                         std::uint8_t layer) {
  const std::string layerPath = layer == 0 ? "" : "layer_" + std::to_string(layer) + "/";
  return std::string(ownerId) + "/__vegetation_cells/" + layerPath + std::to_string(x) +
         "/" + std::to_string(z);
}

TerrainVegetationWorld::TerrainVegetationWorld(
    JobSystem &jobs, TerrainVegetationResidency::Settings settings,
    TerrainVegetationResidency::Load source, std::string ownerId,
    std::string paletteId, std::filesystem::path projectDirectory)
    : residency_(jobs, settings, std::move(source),
                 [ownerId, paletteId, projectDirectory](
                     const auto &key, auto &placements, std::stop_token stop) {
                   return prepareTerrainCell(
                       placements,
                       terrainVegetationCellOwnerId(ownerId, key.x, key.z, key.layer),
                       paletteId, projectDirectory, stop);
                 }),
      ownerId_(std::move(ownerId)), paletteId_(std::move(paletteId)),
      maximumPublications_(settings.maximumPublicationsPerUpdate) {
  if (ownerId_.empty() || paletteId_.empty()) {
    throw std::invalid_argument(
        "Vegetation publication requires owner and palette IDs");
  }
}

bool TerrainVegetationWorld::update(World &world,
                                    std::span<const Demand> demand,
                                    RuntimePrefabService *prefabs,
                                    std::string &error) {
  error.clear();
  try {
    const auto *root = findEntity(world, ownerId_);
    if (!root || !root->enabled) {
      demand = {};
    }
    std::vector<TerrainVegetationResidency::Demand> requests;
    std::set<std::tuple<std::int64_t, std::int64_t, std::uint8_t>> wanted;
    for (const auto &cell : demand) {
      if (!std::isfinite(cell.origin.x) || !std::isfinite(cell.origin.y) ||
          !std::isfinite(cell.origin.z)) {
        throw std::invalid_argument("Vegetation cell origin must be finite");
      }
      requests.push_back(cell.residency);
      if (!wanted.emplace(cell.residency.key.x, cell.residency.key.z, cell.residency.key.layer).second ||
          cell.residency.preparationBytes == 0 ||
          (cell.residency.key.storage != TerrainVegetationStorage::Stored &&
           cell.residency.key.storage !=
               TerrainVegetationStorage::Procedural)) {
        throw std::invalid_argument(
            "Invalid or duplicate vegetation cell demand");
      }
    }

    std::erase_if(failures_, [&](const auto &failure) {
      return std::ranges::none_of(requests, [&](const auto &request) {
        return request.key == failure.first;
      });
    });

    CellPublication eviction(world, prefabs);
    for (const auto &entity : world.entities) {
      const auto *cell = entity.component<TerrainVegetationCellOwner>();
      if (cell && cell->regionOwner == ownerId_ &&
          !wanted.contains({cell->key.x, cell->key.z, cell->key.layer})) {
        eviction.remove(entity.id);
      }
    }
    if (!eviction.commands.empty()) {
      eviction.commit();
    }

    residency_.update(requests);
    awaitingPublication_ = false;
    std::size_t publications = 0;
    for (const auto &cell : demand) {
      const auto &key = cell.residency.key;
      const auto loadingError = residency_.error(key);
      if (!loadingError.empty()) {
        error = loadingError;
        return false;
      }
      const auto failed = failures_.find(key);
      if (failed != failures_.end()) {
        error = failed->second;
        return false;
      }
      const auto *placements = residency_.find(key);
      if (!placements) {
        continue;
      }
      const auto cellId = terrainVegetationCellOwnerId(ownerId_, key.x, key.z, key.layer);
      const auto *existing = findEntity(world, cellId);
      const auto *marker =
          existing ? existing->component<TerrainVegetationCellOwner>()
                   : nullptr;
      if (existing && (!marker || marker->regionOwner != ownerId_)) {
        throw std::runtime_error("Vegetation cell ID is already owned: " +
                                 cellId);
      }
      const auto *transform =
          existing ? existing->component<Transform3DComponent>() : nullptr;
      if (marker && marker->key == key && transform &&
          samePosition(transform->position, cell.origin)) {
        continue;
      }
      if (publications == maximumPublications_) {
        awaitingPublication_ = true;
        break;
      }
      // Reacquire after each publication: entity storage can relocate.
      root = findEntity(world, ownerId_);
      if (!root) {
        throw std::runtime_error(
            "Vegetation owner disappeared during publication");
      }
      Entity owner;
      owner.id = cellId;
      owner.name = "Vegetation cell";
      owner.layer = root->layer;
      owner.sceneOwner = root->sceneOwner;
      owner.setComponent(
          Transform3DComponent{.parent = ownerId_, .position = cell.origin});
      owner.setComponent(TerrainVegetationCellOwner{ownerId_, key});
      CellPublication publication(world, prefabs);
      if (!publication.commands.create(world, std::move(owner),
                                       existing != nullptr)) {
        throw std::runtime_error("Cannot stage vegetation cell " + cellId);
      }
      const auto *terrain = root->component<Terrain3DComponent>();
      const std::span<const std::string> exclusions =
          terrain ? std::span<const std::string>(terrain->scatterExclusions)
                  : std::span<const std::string>{};
      const auto *prepared = residency_.prepared(key);
      if (prepared) {
        // Snapshot the pending owner before adoption can reallocate commands.
        const Entity ownerSnapshot =
            *publication.commands.pendingEntity(cellId);
        publishTerrainVisuals(world, publication.commands, ownerSnapshot,
                              *prepared, exclusions);
      }
      const TerrainScatterView view{paletteId_, *placements, exclusions,
                                    prepared};
      const TerrainPrefabServiceProvider provider = [&] {
        return publication.prefabs();
      };
      TerrainScatterSyncStats stats;
      if (prepared) {
        stats = syncTerrainScatterEntities(world, publication.commands, cellId,
                                           view, provider, error);
      } else {
        stats = syncTerrainScatter(world, publication.commands, cellId, view,
                                   provider, error);
      }
      if (!error.empty() || stats.unresolved != 0) {
        if (error.empty()) {
          error = "Vegetation cell has unresolved prefab placements";
        }
        failures_[key] = error;
        return false;
      }
      publication.commit();
      ++publications;
    }
    return true;
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}

bool TerrainVegetationWorld::clear(World &world, RuntimePrefabService *prefabs,
                                   std::string &error) {
  return update(world, {}, prefabs, error);
}

void TerrainVegetationWorld::drain() { residency_.clear(); }

TerrainVegetationResidency::Statistics
TerrainVegetationWorld::statistics() const {
  return residency_.statistics();
}
bool TerrainVegetationWorld::hasPendingWork() const {
  const auto state = residency_.statistics();
  return awaitingPublication_ || state.pendingJobs != 0 || state.retiringJobs != 0;
}

} // namespace demi::runtime
