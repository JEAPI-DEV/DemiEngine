#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include <map>
#include <nlohmann/json.hpp>

namespace demi::runtime {

struct TerrainBiome {
  float baseHeight = 0;
  float heightVariation = 8;
  float featureSize = 32;
  float roughness = .5F;
  int octaves = 4;
  Color color{.35F, .55F, .25F, 1};
};

enum class TerrainLayerKind {
  Generation,
  Biome,
  Sculpt,
  Protection,
  Exclusion
};

struct TerrainLayer {
  std::string id;
  std::string name;
  TerrainLayerKind kind = TerrainLayerKind::Sculpt;
  bool enabled = true;
};

struct TerrainRegion {
  std::string biome = "default";
  Vec2 center{}; // Local XZ, encoded as [x,z].
  float radius = 8;
  float strength = 1;
  float falloff = 1;
  std::string layer = "biomes";
};

enum class TerrainEditKind { Raise, Lower, Flatten, Smooth, Protect };

struct TerrainProtectionSample {
  Vec2 position{};
  float height = 0;
};

struct TerrainEdit {
  TerrainEditKind kind = TerrainEditKind::Raise;
  Vec2 center{};
  float radius = 8;
  float strength = 1;
  float falloff = 1;
  float amount = 1;
  float targetHeight = 0;
  // Protection is absolute and survives seed/biome changes. Grid changes
  // require explicit recapture; snapshot spacing/extent must match the recipe.
  // Every sample with nonzero brush weight is captured at full height and
  // locked against subsequent edits; strength/falloff define patch membership.
  Vec2 snapshotSize{};
  int snapshotCellsX = 0;
  int snapshotCellsZ = 0;
  std::vector<TerrainProtectionSample> samples;
  // Empty selects "protection" for Protect, otherwise "sculpt".
  std::string layer;
};

struct TerrainExclusion {
  Vec2 center{};
  float radius = 8;
  float strength = 1;
  float falloff = 1;
  float value = 1;
  std::string layer = "exclusions";
};

struct TerrainRecipe {
  static constexpr int formatVersion = 1;
  Vec2 size{128, 128};
  int cellsX = 128;
  int cellsZ = 128;
  int seed = 1337;
  int chunkCells = 32;
  std::string defaultBiome = "default";
  std::map<std::string, TerrainBiome> biomes{{"default", TerrainBiome{}}};
  std::vector<TerrainLayer> layers{
      {"generation", "Generation", TerrainLayerKind::Generation, true},
      {"biomes", "Biomes", TerrainLayerKind::Biome, true},
      {"sculpt", "Sculpt", TerrainLayerKind::Sculpt, true},
      {"protection", "Protection", TerrainLayerKind::Protection, true},
      {"exclusions", "Exclusions", TerrainLayerKind::Exclusion, true}};
  std::vector<TerrainRegion> regions;
  std::vector<TerrainEdit> edits;
  std::vector<TerrainExclusion> exclusions;

  static TerrainRecipe parse(const nlohmann::json &json);
  static nlohmann::json defaults();
  nlohmann::json toJson() const;
  void validate() const;
  std::size_t sampleCount() const;
};

// Compact-support radial brush. Falloff 0 is a hard disk; otherwise
// strength * (1-distance/radius)^falloff. All brush strengths are in [0,1].
float terrainBrushWeight(Vec2 position, Vec2 center, float radius,
                         float strength, float falloff);

} // namespace demi::runtime
