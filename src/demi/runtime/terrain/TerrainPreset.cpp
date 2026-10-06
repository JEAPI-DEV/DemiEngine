#include "demi/runtime/terrain/TerrainPreset.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/DataDocument.h"
#include "demi/assets/DataValueRead.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace demi::runtime {
namespace {

// The document kind every preset rejection is prefixed with. Passing it to the
// shared readers reproduces reject()'s prefix exactly.
constexpr std::string_view Kind = "Terrain preset";

[[noreturn]] void reject(std::string_view pointer, std::string_view what) {
  throw std::invalid_argument("Terrain preset " + std::string(pointer) + ": " +
                              std::string(what));
}

void requireAt(bool condition, std::string_view pointer, std::string_view what) {
  if (!condition)
    reject(pointer, what);
}

// DataValue exposes its variant directly and has no JSON projection, so rules
// are converted back before reusing the authored rule parser. Keeping that
// conversion local avoids a second rule grammar in the preset path.
nlohmann::json toJson(const assets::DataValue &value) {
  if (std::holds_alternative<bool>(value.value))
    return std::get<bool>(value.value);
  if (std::holds_alternative<std::int64_t>(value.value))
    return std::get<std::int64_t>(value.value);
  if (std::holds_alternative<double>(value.value))
    return std::get<double>(value.value);
  if (std::holds_alternative<std::string>(value.value))
    return std::get<std::string>(value.value);
  if (const auto *entries = value.array()) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto &entry : *entries)
      result.push_back(toJson(entry));
    return result;
  }
  if (const auto *entries = value.object()) {
    nlohmann::json result = nlohmann::json::object();
    for (const auto &[key, entry] : *entries)
      result[key] = toJson(entry);
    return result;
  }
  return nullptr;
}

std::string readString(const assets::DataValue &root, std::string_view key,
                       std::string_view pointer, bool required,
                       std::string fallback = {}) {
  const auto *value = assets::data_read::field(root, key);
  if (value == nullptr || value->isNull()) {
    requireAt(!required, pointer, "is required");
    return fallback;
  }
  const std::string text = assets::data_read::text(*value, Kind, pointer);
  // A preset rejects an empty string for an optional field too, which the
  // shared reader only does when the field is required.
  requireAt(!text.empty(), pointer, "must not be empty");
  return text;
}

float readNumber(const assets::DataValue &root, std::string_view key,
                 std::string_view pointer, float fallback) {
  const auto *value = assets::data_read::field(root, key);
  if (value == nullptr || value->isNull())
    return fallback;
  return assets::data_read::number(*value, Kind, pointer);
}

int readInt(const assets::DataValue &root, std::string_view key,
            std::string_view pointer, int fallback) {
  const auto *value = assets::data_read::field(root, key);
  if (value == nullptr || value->isNull())
    return fallback;
  return assets::data_read::integer(*value, Kind, pointer);
}

Vec2 readVec2(const assets::DataValue &root, std::string_view key,
              std::string_view pointer, Vec2 fallback) {
  const auto *value = assets::data_read::field(root, key);
  if (value == nullptr || value->isNull())
    return fallback;
  return assets::data_read::vec2(*value, Kind, pointer);
}

// data_read::color would additionally require every channel to sit in [0, 1],
// which is a rule this document has never enforced, so the shape and the
// per-channel decoding are kept here.
Color readColor(const assets::DataValue &root, std::string_view key,
                std::string_view pointer, Color fallback) {
  const auto *value = assets::data_read::field(root, key);
  if (value == nullptr || value->isNull())
    return fallback;
  const auto *entries = value->array();
  requireAt(entries != nullptr && entries->size() == 4, pointer,
            "must be an [r, g, b, a] quad");
  Color color = fallback;
  float *channels[] = {&color.r, &color.g, &color.b, &color.a};
  for (std::size_t index = 0; index < 4; ++index) {
    requireAt((*entries)[index].isNumber(), pointer,
              "must contain four numbers");
    *channels[index] =
        assets::data_read::number((*entries)[index], Kind, pointer);
  }
  return color;
}

void readLayers(const assets::DataValue &root, TerrainPreset &preset) {
  const auto *value = assets::data_read::field(root, "layers");
  if (value == nullptr || value->isNull())
    return;
  const auto *entries = value->array();
  requireAt(entries != nullptr, "layers", "must be an array");
  for (const auto &entry : *entries) {
    requireAt(entry.isObject(), "layers", "must contain layer objects");
    const auto *id = assets::data_read::field(entry, "id");
    const auto pointer =
        "layers/" + (id == nullptr || !id->isString()
                         ? std::string()
                         : std::get<std::string>(id->value));
    TerrainLayer layer;
    layer.id = readString(entry, "id", pointer, true);
    layer.name = readString(entry, "name", pointer, false, layer.id);
    // The recipe owns the only accepted kind names, so a preset cannot author a
    // layer kind that the recipe would later reject.
    layer.kind = layerKind(readString(entry, "kind", pointer, false, "sculpt"));
    const auto *enabled = assets::data_read::field(entry, "enabled");
    layer.enabled = enabled == nullptr || enabled->isNull() ||
                    std::get<bool>(enabled->value);
    preset.layers.push_back(std::move(layer));
  }
}

void readRules(const assets::DataValue &root, TerrainPreset &preset) {
  const auto *value = assets::data_read::field(root, "rules");
  if (value == nullptr || value->isNull())
    return;
  const auto *entries = value->array();
  requireAt(entries != nullptr, "rules", "must be an array");
  for (const auto &entry : *entries) {
    requireAt(entry.isObject(), "rules", "must contain rule objects");
    // The rule parser is the authored one, so a preset rule cannot express
    // anything an authored rule cannot, and both reject the same mistakes.
    preset.rules.push_back(TerrainBiomeRule::parse(toJson(entry)));
  }
}

} // namespace

nlohmann::json terrainPresetFragment(const TerrainPreset &preset) {
  // Round-tripping through TerrainRecipe's own serializer is deliberate: the
  // fragment is validated by exactly the same code path as an authored recipe,
  // so the two cannot drift apart over time.
  TerrainRecipe recipe;
  recipe.size = preset.size;
  recipe.cellsX = preset.cellsX;
  recipe.cellsZ = preset.cellsZ;
  recipe.chunkCells = preset.chunkCells;
  recipe.seed = preset.seed;
  recipe.defaultBiome = preset.defaultBiome;
  // Collections are replaced only when the preset actually supplies them, so a
  // preset that omits layers keeps the canonical default set instead of
  // producing a recipe with no generation layer. Scalars need no such guard:
  // a preset that leaves one at its default value is indistinguishable from
  // omitting it, which is exactly the intent.
  if (!preset.biomes.empty())
    recipe.biomes = preset.biomes;
  if (!preset.landforms.empty())
    recipe.landforms = preset.landforms;
  recipe.defaultLandform = preset.defaultLandform;
  if (!preset.layers.empty())
    recipe.layers = preset.layers;
  if (!preset.rules.empty())
    recipe.rules = preset.rules;

  const nlohmann::json fragment = recipe.toJson();
  nlohmann::json result = nlohmann::json::object();
  // The fragment is itself a valid recipe, so it carries the document header
  // even though format_version is framing rather than something a preset
  // configures.
  result["format_version"] = TerrainRecipe::formatVersion;
  for (const auto key : terrainPresetGenerationKeys)
    if (fragment.contains(key))
      result[key] = fragment.at(key);
  return result;
}

nlohmann::json applyTerrainPreset(const nlohmann::json &authoredRecipe,
                                  const TerrainPreset &preset) {
  // Generation keys are replaced by the preset by design, so the authored
  // document is deliberately not validated on its own: its strokes only make
  // sense against the biomes the preset supplies, and a region painted with a
  // preset biome is perfectly valid once applied. The merged result below is
  // validated instead, which checks the strokes against the biomes they will
  // actually be generated with.

  nlohmann::json merged = terrainPresetFragment(preset);
  // Strokes are authorial content. A preset replaces how the landform is
  // generated, never what the author painted or sculpted on top of it, so
  // applying a preset can never destroy work.
  for (const auto key : {"regions", "edits", "exclusions"})
    if (authoredRecipe.contains(key))
      merged[key] = authoredRecipe.at(key);
  merged["preset_id"] = preset.id;
  merged["preset_version"] = preset.formatVersion;

  // The merged document must itself be a valid recipe, so a malformed preset
  // is rejected at the boundary rather than at generation time.
  (void)TerrainRecipe::parse(merged);
  return merged;
}

std::optional<TerrainPreset>
parseTerrainPreset(const assets::DataDocument &document, std::string_view id) {
  const auto &root = document.root();
  requireAt(root.isObject(), "", "must be an object");

  TerrainPreset preset;
  preset.id = id;
  preset.version = readInt(root, "format_version", "format_version", 0);
  requireAt(preset.version == TerrainPreset::formatVersion,
            "format_version", "must be 1");
  preset.name = readString(root, "name", "name", true);
  preset.label = readString(root, "label", "label", false, preset.name);
  preset.description = readString(root, "description", "description", false);

  preset.size = readVec2(root, "size", "size", preset.size);
  preset.cellsX = readInt(root, "cells_x", "cells_x", preset.cellsX);
  preset.cellsZ = readInt(root, "cells_z", "cells_z", preset.cellsZ);
  preset.chunkCells = readInt(root, "chunk_cells", "chunk_cells",
                              preset.chunkCells);
  preset.seed = readInt(root, "seed", "seed", preset.seed);
  preset.defaultBiome =
      readString(root, "default_biome", "default_biome", false,
                 preset.defaultBiome);
  preset.defaultLandform = readString(root, "default_landform",
                                      "default_landform", false,
                                      preset.defaultLandform);

  if (const auto *biomes = assets::data_read::field(root, "biomes")) {
    requireAt(biomes->isObject() && !biomes->object()->empty(), "biomes",
              "must be a non-empty object keyed by biome name");
    for (const auto &[name, entry] : *biomes->object()) {
      const auto pointer = "biomes/" + name;
      requireAt(entry.isObject(), pointer, "must be an object");
      TerrainBiome biome;
      biome.landform = readString(entry, "landform", pointer + "/landform", false);
      biome.material =
          readString(entry, "material", pointer + "/material", false);
      biome.textureScale = readNumber(entry, "texture_scale",
                                      pointer + "/texture_scale", 1.F);
      biome.color = readColor(entry, "color", pointer + "/color", biome.color);
      preset.biomes.emplace(name, biome);
    }
  }
  // Landforms carry the ground shape, so a preset declares them alongside the
  // biomes rather than folding elevation into each biome.
  if (const auto *landforms = assets::data_read::field(root, "landforms")) {
    requireAt(landforms->isObject() && !landforms->object()->empty(), "landforms",
              "must be a non-empty object keyed by landform name");
    for (const auto &[name, entry] : *landforms->object()) {
      const auto pointer = "landforms/" + name;
      requireAt(entry.isObject(), pointer, "must be an object");
      TerrainLandform shape;
      shape.baseHeight =
          readNumber(entry, "base_height", pointer + "/base_height", shape.baseHeight);
      shape.heightVariation = readNumber(entry, "height_variation",
                                         pointer + "/height_variation",
                                         shape.heightVariation);
      shape.featureSize = readNumber(entry, "feature_size",
                                     pointer + "/feature_size", shape.featureSize);
      shape.roughness =
          readNumber(entry, "roughness", pointer + "/roughness", shape.roughness);
      shape.octaves = readInt(entry, "octaves", pointer + "/octaves", shape.octaves);
      requireAt(shape.featureSize > 0.F, pointer + "/feature_size",
                "must be greater than 0");
      requireAt(shape.octaves >= 1 && shape.octaves <= 16, pointer + "/octaves",
                "must be between 1 and 16");
      requireAt(shape.roughness >= 0.F && shape.roughness <= 1.F,
                pointer + "/roughness", "must be within [0, 1]");
      preset.landforms.emplace(name, shape);
    }
  }
  readLayers(root, preset);
  readRules(root, preset);

  // Validate the assembled preset as if it were an authored recipe, so a preset
  // that references an unknown biome or a missing layer cannot be applied.
  TerrainRecipe probe;
  probe.size = preset.size;
  probe.cellsX = preset.cellsX;
  probe.cellsZ = preset.cellsZ;
  probe.chunkCells = preset.chunkCells;
  probe.seed = preset.seed;
  probe.defaultBiome = preset.defaultBiome;
  probe.defaultLandform = preset.defaultLandform;
  // Same emptiness guards as terrainPresetFragment: a preset that omits a
  // collection keeps the canonical default set rather than producing a recipe
  // with, for example, no generation layer.
  if (!preset.biomes.empty())
    probe.biomes = preset.biomes;
  if (!preset.landforms.empty())
    probe.landforms = preset.landforms;
  if (!preset.layers.empty())
    probe.layers = preset.layers;
  if (!preset.rules.empty())
    probe.rules = preset.rules;
  probe.validate();
  return preset;
}

std::optional<TerrainPreset> loadTerrainPreset(const AssetRegistry &registry,
                                               std::string_view id) {
  const auto *manifest = findAsset(registry, std::string(id));
  if (manifest == nullptr)
    throw std::invalid_argument("Terrain preset was not found: " +
                                std::string(id));
  if (manifest->type != "DataAsset")
    throw std::invalid_argument("Terrain preset must be a DataAsset: " +
                                std::string(id) + " is " + manifest->type);
  const auto metadata = assets::dataAssetMetadata(*manifest);
  if (!metadata || metadata->contentType != "terrain_preset")
    throw std::invalid_argument(
        "Terrain preset must declare settings.content_type \"terrain_preset\": " +
        std::string(id));
  const auto loaded = assets::loadDataAsset(*manifest);
  if (!loaded)
    throw std::invalid_argument("Terrain preset could not be read: " +
                                std::string(id));
  return parseTerrainPreset(*loaded->document, manifest->id);
}

} // namespace demi::runtime
