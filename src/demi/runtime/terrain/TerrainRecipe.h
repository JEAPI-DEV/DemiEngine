#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

namespace demi::runtime {

// How the ground is shaped. Landform carries elevation and nothing about
// appearance, so restyling a biome never means reshaping the terrain.
struct TerrainLandform {
  float baseHeight = 0;
  float heightVariation = 8;
  float featureSize = 32;
  float roughness = .5F;
  int octaves = 4;
};

// How a biome looks. The biome is an appearance choice assigned by rules from
// the finished surface; it points at a landform rather than owning elevation, so
// several biomes can share one shape and one shape can serve several looks.
struct TerrainBiome {
  // Landform this biome's ground is shaped by. Empty means the recipe's default.
  std::string landform;
  Color color{.35F, .55F, .25F, 1};
  // Optional material asset, applied alongside the tint where a surface has one.
  std::string material;
  // Texture repetitions per terrain-local unit.
  float textureScale = 1;
};

// Substrate is derived from the finished field, not authored, so rules can key
// on material without a second painting pass.
enum class TerrainSubstrate { Soil, Rock, Sand, Wet };

std::string_view terrainSubstrateName(TerrainSubstrate substrate);
std::optional<TerrainSubstrate> terrainSubstrateFromName(
    std::string_view name);

// An inclusive [minimum, maximum] band. Omitted conditions are unconstrained.
struct TerrainRuleBand {
  bool enabled = false;
  float minimum = 0;
  float maximum = 0;

  bool contains(float value) const;
  static TerrainRuleBand parse(const nlohmann::json &json,
                               std::string_view field);
  nlohmann::json toJson() const;
};

// One automatic biome assignment rule.
//
// Rules are evaluated against a derived field context, not against the recipe's
// painted strokes. On conflict the highest priority wins; an equal priority is
// broken by blend weight, and a full tie keeps the earlier rule, so assignment
// never depends on evaluation order. Painted regions are
// applied after rules and always win, which keeps manual override
// authoritative over automatic assignment.
struct TerrainRecipe;
struct TerrainBiomeRule {
  std::string id;
  // Biome layer this rule belongs to. Disabling the layer removes its rules
  // from evaluation, matching how regions already behave.
  std::string layer = "biomes";
  std::string biome;
  int priority = 0;
  // Transition width in world units, applied along elevation only. 0 assigns
  // hard boundaries; larger values ramp the blend between neighbours.
  float blend = 0;
  TerrainRuleBand elevation;
  TerrainRuleBand slope;
  TerrainRuleBand moisture;
  // Distance in world units to the nearest sample at or below sea level.
  TerrainRuleBand waterDistance;
  std::vector<TerrainSubstrate> substrate;

  static TerrainBiomeRule parse(const nlohmann::json &json);
  nlohmann::json toJson() const;
  void validate(const TerrainRecipe &recipe) const;
};

enum class TerrainLayerKind {
  Generation,
  Biome,
  Sculpt,
  Protection,
  Exclusion
};

// The single layer-kind vocabulary. Exported so a preset, an authored recipe and
// the editor cannot disagree about the accepted kind names.
TerrainLayerKind layerKind(const std::string &name);
const char *layerName(TerrainLayerKind kind);

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
  // Ground shapes, keyed by name. Elevation comes from these, never from a
  // biome, so appearance and shape are edited and reasoned about separately.
  std::map<std::string, TerrainLandform> landforms{{"default", TerrainLandform{}}};
  // Landform used where no biome contributes a shape. Must name a landform.
  std::string defaultLandform = "default";
  std::vector<TerrainLayer> layers{
      {"generation", "Generation", TerrainLayerKind::Generation, true},
      {"biomes", "Biomes", TerrainLayerKind::Biome, true},
      {"sculpt", "Sculpt", TerrainLayerKind::Sculpt, true},
      {"protection", "Protection", TerrainLayerKind::Protection, true},
      {"exclusions", "Exclusions", TerrainLayerKind::Exclusion, true}};
  std::vector<TerrainRegion> regions;
  std::vector<TerrainEdit> edits;
  std::vector<TerrainExclusion> exclusions;
  // Optional automatic biome assignment. Empty keeps the field on the authored
  // default biome plus painted regions, which is the pre-milestone-2 behaviour.
  std::vector<TerrainBiomeRule> rules;
  // Provenance: which landscape preset was applied to produce this recipe.
  // Recorded so a recipe is self-describing and regenerates identically. It is
  // not a runtime override; the values above are the single source of truth.
  std::string presetId;
  int presetVersion = 0;
  // asset:// id of the terrain_palette to scatter. Empty scatters nothing, so a
  // recipe without a palette is exactly as before. The palette itself is
  // loaded by the caller, which owns the asset registry; generation receives
  // the resolved palette rather than reaching for assets itself.
  std::string paletteId;
  // Optional authored generation graph. Manual regions/sculpt/protection stay
  // in this recipe and are applied over the graph's generated base.
  nlohmann::json graph;

  static TerrainRecipe parse(const nlohmann::json &json);
  static nlohmann::json defaults();
  nlohmann::json toJson() const;
  // Cache identity omits mesh-only appearance. Color stays because the field
  // stores biome colors and a color edit must refresh those values.
  nlohmann::json generationJson() const;
  void validate() const;
  std::size_t sampleCount() const;

  // Locality contract for incremental editing.
  //
  // sameGenerationInputs() is the only place that decides whether a recipe
  // change may be replayed locally or must rebuild the whole base. It is
  // declared here, beside the fields it classifies, so that adding a generation
  // input is one obvious step rather than an edit to a helper in another file.
  //
  // Rule: every field that the procedural base reads must be listed here as a
  // global input. Fields that only affect the surface (regions, edits,
  // exclusions, protection, biome tint, non-generation layer toggles) and
  // layout-only fields (chunkCells) are deliberately absent, because replaying
  // them locally is correct. Adding a nonlocal stage such as erosion or
  // hydrology means adding its parameters here, so that strokes force a full
  // rebuild instead of silently producing a seam.
  //
  // demi-terrain-locality-tests perturbs every input listed and every field
  // deliberately excluded, so forgetting either side fails the gate.
  bool sameGenerationInputs(const TerrainRecipe &other) const;
};

// Compact-support radial brush. Falloff 0 is a hard disk; otherwise
// strength * (1-distance/radius)^falloff. All brush strengths are in [0,1].
float terrainBrushWeight(Vec2 position, Vec2 center, float radius,
                         float strength, float falloff);

} // namespace demi::runtime
