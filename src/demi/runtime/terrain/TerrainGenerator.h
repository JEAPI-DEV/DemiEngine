#pragma once

#include "demi/runtime/terrain/TerrainPalette.h"
#include "demi/runtime/terrain/TerrainScatterPlacement.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "demi/runtime/terrain/TerrainSamples.h"
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
inline constexpr int terrainGeneratorVersion = 4;

struct TerrainChunk {
  int firstCellX = 0;
  int firstCellZ = 0;
  int cellsX = 0;
  int cellsZ = 0;
};

struct HeightField {
  Vec2 size{};
  int cellsX = 0;
  int cellsZ = 0;
  TerrainSamples<float> baseHeights;
  TerrainSamples<float> heights;
  TerrainSamples<Vec3> normals;
  TerrainSamples<std::size_t> biomeIndices;
  TerrainSamples<float> exclusions;
  std::vector<std::string> biomeIds;
  std::vector<Color> biomeColors;
  std::vector<std::string> biomeMaterials;
  std::vector<float> biomeTextureScales;
  const std::string &biomeMaterial(std::size_t index) const {
    static const std::string defaultMaterial;
    return biomeMaterials.empty() ? defaultMaterial : biomeMaterials.at(index);
  }
  float biomeTextureScale(std::size_t index) const {
    return biomeTextureScales.empty() ? 1.F : biomeTextureScales.at(index);
  }
  std::vector<TerrainChunk> chunks;
  // Resolved palette instances, populated only when the recipe names a palette.
  // Kept as descriptions rather than entities so generation stays testable
  // without a renderer or a world; the consumer instantiates them.
  std::vector<TerrainScatterPlacement> scatterPlacements;
  bool scatterTruncated = false;
  // The palette that produced scatterPlacements, for diagnostics.
  std::string paletteId;
  // Content hash of every input asset this field was generated from. An asset id
  // does not change when the file's contents do, so this is what makes a stale
  // field detectable rather than merely unlikely.
  std::string inputFingerprint;
  // The stage order that produced this field, so a consumer can tell a preview
  // from a complete result instead of assuming every stage ran.
  std::string stageOrder;
  std::string quality = "standard";
  std::shared_ptr<const TerrainGraphArtifacts> graphArtifacts;
  std::shared_ptr<const TerrainPalette> resolvedPalette;

  // Row-major global samples; adjacent chunks reference the same edge samples.
  std::size_t index(int x, int z) const;
  Vec2 position(int x, int z) const;
  float height(int x, int z) const;
  Vec3 normal(int x, int z) const;
};

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
