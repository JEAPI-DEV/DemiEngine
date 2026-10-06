#pragma once

#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainWater.h"
#include <memory>
#include <optional>
#include <string>

namespace demi::runtime {
class TerrainGraphEvaluationCache;
struct TerrainGraphNodeRun {
  std::string id;
  bool cached = false;
  double milliseconds = 0;
};
struct TerrainGraphArtifacts {
  // Graph output before manual regions, sculpt and exclusions. Cache fields do
  // not own artifacts, so retaining this checkpoint cannot form a cycle.
  std::shared_ptr<const HeightField> baseField;
  // Aliases cached Instances output; artifact copies share immutable candidates.
  std::shared_ptr<const std::vector<TerrainScatterPlacement>> basePlacements;
  std::optional<TerrainWaterResult> waterResult;
  TerrainWaterAuthoring water;
  std::vector<TerrainGraphNodeRun> nodes;
  std::vector<std::string> warnings;
  std::shared_ptr<const TerrainGraphEvaluationCache> cache;
};

// Replays enabled painted regions over the labels from a graph output. Heights
// remain owned by graph nodes, even when a painted biome names another landform.
class TerrainGraphBiomeOverlay {
public:
  TerrainGraphBiomeOverlay(const HeightField &base,
                           const TerrainRecipe &recipe);
  [[nodiscard]] std::size_t biomeAt(int x, int z) const;

private:
  const HeightField &base_;
  std::vector<const TerrainRegion *> regions_;
  std::vector<std::size_t> regionBiomes_;
};
struct TerrainGraphInputs {
  const TerrainPalette *palette = nullptr;
  std::string inputFingerprint;
  std::shared_ptr<const TerrainGraphEvaluationCache> previousCache;
};
std::optional<HeightField> executeTerrainGraph(
    const TerrainRecipe &recipe, const TerrainGraphInputs &inputs = {},
    std::stop_token stop = {}, const TerrainGenerator::Progress &progress = {});
} // namespace demi::runtime
