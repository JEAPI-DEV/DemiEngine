#pragma once
#include "demi/runtime/terrain/RegionalTerrainSampler.h"
#include "demi/runtime/terrain/TerrainVegetationWorld.h"

namespace demi::runtime {
// Chooses nearby vegetation demand independently from distant surface LOD.
// The sampler and palette are immutable snapshots shared with worker jobs.
class RegionalTerrainVegetation {
public:
  RegionalTerrainVegetation(
      JobSystem &jobs, std::shared_ptr<const RegionalTerrainSampler> sampler,
      std::string owner);
  bool update(World &world, Vec3 localCamera, RuntimePrefabService *prefabs,
              std::string &error, bool includeCanopy = true);
  bool clear(World &world, RuntimePrefabService *prefabs, std::string &error);
  void drain();
  [[nodiscard]] bool hasPendingWork() const;
  TerrainVegetationResidency::Statistics statistics() const;

private:
  std::shared_ptr<const RegionalTerrainSampler> sampler_;
  TerrainVegetationWorld cells_;
};
} // namespace demi::runtime
