#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataDocument.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {

// A landscape preset is a reusable, versioned template that configures the
// shared generator. It is a DataAsset with settings.content_type
// "terrain_preset", referenced by a normal asset:// URI.
//
// A preset is NOT a second generator and NOT a recipe override applied at
// runtime. It is a recipe fragment that an author applies into their recipe
// once; the recipe stays the single source of truth for generation. That keeps
// incremental editing, the locality contract and undo/redo trivially correct,
// and means a preset can never disagree with the terrain it is applied to.
//
// Applying records the preset identity and version in the recipe, so a recipe
// is self-describing: the same recipe regenerates the same terrain forever,
// and changing the applied preset is a normal recipe change.
struct TerrainPreset {
  static constexpr int formatVersion = 1;

  std::string id;
  // The document version this preset was loaded from. Always formatVersion
  // today; it is carried per-instance so a future v2 can round-trip.
  int version = formatVersion;
  std::string name;
  std::string description;
  // Optional icon/label shown in the editor's preset picker.
  std::string label;

  // The generation keys a preset may supply. Strokes are deliberately absent:
  // regions, edits and exclusions are authorial content and are never
  // templated, so applying a preset cannot destroy someone's sculpting.
  Vec2 size = {128, 128};
  int cellsX = 128;
  int cellsZ = 128;
  int chunkCells = 32;
  int seed = 1337;
  std::string defaultBiome = "default";
  std::map<std::string, TerrainBiome> biomes;
  std::map<std::string, TerrainLandform> landforms;
  std::string defaultLandform = "default";
  std::vector<TerrainLayer> layers;
  std::vector<TerrainBiomeRule> rules;
};

// The recipe keys a preset supplies, in canonical order.
constexpr std::string_view terrainPresetGenerationKeys[]{
    "size",     "resolution", "chunk_cells", "seed",     "default_biome",
    "default_landform",       "biomes",      "landforms", "layers",
    "rules"};

// Merges a preset into an authored recipe and records provenance.
//
// Generation keys come from the preset. The authored recipe's strokes
// (regions, edits, exclusions) are always preserved: a preset replaces how the
// landform is generated, never what the author painted on top of it. The
// result is stamped with "preset_id" and "preset_version" so provenance
// survives in authored source and the recipe remains self-describing.
//
// The input is the authored recipe JSON, which may be sparse. Omitted
// generation keys therefore default rather than being lost, and the returned
// JSON is a complete recipe.
nlohmann::json applyTerrainPreset(const nlohmann::json &authoredRecipe,
                                  const TerrainPreset &preset);

// The recipe fragment a preset supplies, before merging.
nlohmann::json terrainPresetFragment(const TerrainPreset &preset);

// Loads a preset by asset id. Throws std::invalid_argument with an actionable
// message if the asset is missing, is not a DataAsset, is not a terrain preset,
// or the document is malformed.
std::optional<TerrainPreset> loadTerrainPreset(const AssetRegistry &registry,
                                               std::string_view id);

// Parses a preset document that has already been read. Kept separate from
// loadTerrainPreset so a caller holding a DataDocument (hot reload, Lua
// Data.load) does not have to re-read the file.
std::optional<TerrainPreset>
parseTerrainPreset(const assets::DataDocument &document, std::string_view id);

} // namespace demi::runtime
