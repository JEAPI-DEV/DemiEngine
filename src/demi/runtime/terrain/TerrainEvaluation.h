#pragma once

#include "demi/runtime/terrain/TerrainRecipe.h"
#include <functional>
#include <memory>

namespace demi::runtime {

struct TerrainBaseSample {
  float height = 0;
  std::size_t biome = 0;
};

// The recipe must outlive this evaluator and remain unchanged while it is used.
// Biome indices follow the sorted iteration order of recipe.biomes.
class TerrainEvaluation {
public:
  explicit TerrainEvaluation(const TerrainRecipe &recipe);
  ~TerrainEvaluation();
  TerrainEvaluation(TerrainEvaluation &&) noexcept;
  TerrainEvaluation &operator=(TerrainEvaluation &&) noexcept;

  TerrainBaseSample baseSample(Vec2 position) const;
  float exclusion(Vec2 position) const;
  // Enabled strokes, ordered by layer and then source order within each layer.
  const std::vector<const TerrainEdit *> &edits() const;
  const std::vector<const TerrainRegion *> &regions() const;
  const std::vector<const TerrainExclusion *> &exclusions() const;
  bool generationEnabled() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Smooth reads the pre-stroke surface through sample. Protection snapshots and
// sample locks are applied by the caller; Protect leaves previous unchanged.
float applyTerrainEdit(const TerrainEdit &edit, Vec2 position, float previous,
                       const std::function<float(int, int)> &sample, int x,
                       int z, int cellsX, int cellsZ);

} // namespace demi::runtime
