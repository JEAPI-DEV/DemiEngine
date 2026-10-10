#pragma once

#include "demi/runtime/terrain/TerrainRecipe.h"
#include "demi/runtime/terrain/TerrainHeightField.h"
#include <functional>
#include <optional>
#include <string_view>
#include <stop_token>

namespace demi::runtime {
struct TerrainGraphArtifacts;

// Bumped whenever the generator's output for an unchanged recipe can change.
// Folded into the terrain generation cache key, so an engine update that alters
// generation can never be served a heightfield produced by the old one.
//
// 2: biome noise moved from the raw world seed to the stable Landform sub-seed
// (see TerrainSeed), which changes the sampled surface for existing recipes.
inline constexpr int terrainGeneratorVersion = 5;

class TerrainGenerator {
public:
  using Progress = std::function<void(float)>;
  // Generates the sampled landform, biome rules/regions and chunk layout.
  // Manual edits and exclusions are deferred so callers can process the base.
  static std::optional<HeightField> generateBase(const TerrainRecipe &recipe,
                                                 std::stop_token stop = {},
                                                 const Progress &progress = {});
  // Applies manual edits and exclusions to the current surface. Retained
  // baseHeights are never replaced by the processed or sculpted heights.
  static bool applyTerrainSurfaceLayers(HeightField &field,
                                        const TerrainRecipe &recipe,
                                        std::stop_token stop = {},
                                        const Progress &progress = {});
  static bool recomputeTerrainNormals(HeightField &field,
                                      std::stop_token stop = {});
  // Empty only on cancellation. Invalid recipes throw invalid_argument;
  // capacity/allocation errors propagate. Progress is monotonic in [0,1].
  static std::optional<HeightField> generate(const TerrainRecipe &recipe,
                                             std::stop_token stop = {},
                                             const Progress &progress = {});

  // Generates and scatters in one pass. The palette is passed in by the caller,
  // which owns the asset registry, so generation never loads assets itself. A
  // recipe without a palette behaves exactly as generate() does.
  static std::optional<HeightField>
  generate(const TerrainRecipe &recipe, const TerrainPalette *palette,
           std::stop_token stop = {}, const Progress &progress = {},
           std::string_view inputFingerprint = {});
};

TerrainEdit createProtectionEdit(const HeightField &field, Vec2 center,
                                 float radius, float strength = 1,
                                 float falloff = 0);

} // namespace demi::runtime
