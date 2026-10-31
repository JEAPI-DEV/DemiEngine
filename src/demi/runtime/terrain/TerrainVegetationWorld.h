#pragma once

#include "demi/runtime/terrain/TerrainVegetationResidency.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace demi::runtime {
struct World;
class RuntimePrefabService;

// Native-only ownership. Its stable ID excludes revision/storage mode so
// changing how a cell loads does not rename its scenery.
struct TerrainVegetationCellOwner {
  std::string regionOwner;
  TerrainVegetationCellKey key;
};

[[nodiscard]] std::string terrainVegetationCellOwnerId(std::string_view ownerId,
                                                       std::int64_t x,
                                                       std::int64_t z,
                                                       std::uint8_t layer = 0);

// Bridges CPU cell residency to ordinary scene batches and prefab ownership.
// Call on the main thread before physics/render synchronization. Each cell's
// origin is relative to the region owner, supplied by the world-coordinate
// owner. This class does not choose demand, rebase the world, or generate
// terrain.
class TerrainVegetationWorld {
public:
  struct Demand {
    TerrainVegetationResidency::Demand residency;
    Vec3 origin;
  };
  TerrainVegetationWorld(JobSystem &jobs,
                         TerrainVegetationResidency::Settings settings,
                         TerrainVegetationResidency::Load source,
                         std::string ownerId, std::string paletteId,
                         std::filesystem::path projectDirectory = {});
  [[nodiscard]] bool update(World &world, std::span<const Demand> demand,
                            RuntimePrefabService *prefabs, std::string &error);
  [[nodiscard]] bool clear(World &world, RuntimePrefabService *prefabs,
                           std::string &error);
  // After clear has removed live ownership, drain cancelled CPU work without
  // touching a replacement world or region with the same stable owner ID.
  void drain();
  [[nodiscard]] bool hasPendingWork() const;
  [[nodiscard]] TerrainVegetationResidency::Statistics statistics() const;

private:
  TerrainVegetationResidency residency_;
  std::string ownerId_;
  std::string paletteId_;
  std::size_t maximumPublications_;
  bool awaitingPublication_ = false;
  std::map<TerrainVegetationCellKey, std::string> failures_;
};
} // namespace demi::runtime
