#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainPreset.h"

namespace demi::runtime {
namespace {
TerrainPreset starter(std::string id, std::string name, std::string description,
                      float amplitude, float featureSize, float roughness,
                      Color color) {
  TerrainPreset preset;
  preset.id = "builtin://terrain/" + id;
  preset.name = std::move(name);
  preset.label = preset.name;
  preset.description = std::move(description);
  // A modest grid keeps a first experiment quick; authors can increase it
  // later.
  preset.cellsX = preset.cellsZ = 64;
  preset.biomes["default"].color = color;
  TerrainGraph graph;
  graph.output = "terrain_output";
  graph.nodes = {
      {"shape",
       amplitude == 0 ? "constant" : "noise",
       amplitude == 0 ? nlohmann::json{{"height", 0}}
                      : nlohmann::json{{"height_variation", amplitude},
                                       {"feature_size", featureSize},
                                       {"roughness", roughness}},
       {40, 320}},
      {"appearance", "biomes", nlohmann::json::object(), {380, 320}},
      {"terrain_output", "output", nlohmann::json::object(), {720, 320}},
      {"shape_help",
       "comment",
       {{"text",
         preset.description +
             (amplitude == 0
                  ? "\nChange Height to raise the building site."
                  : "\nHeight variation controls relief. Feature size controls "
                    "the width of hills. Roughness adds smaller details.")}},
       {40, 20}},
      {"appearance_help",
       "comment",
       {{"text",
         "Biome Rules assigns surface appearances. Open its settings to "
         "change the default biome color or add rules for height and slope. "
         "The starter uses a tint and needs no imported textures."}},
       {380, 20}},
      {"output_help",
       "comment",
       {{"text",
         "Terrain Output publishes the connected surface. Edit nodes, then "
         "Generate to preview. Apply changes to asset saves the shared "
         "terrain. "
         "Comments can be edited, moved or deleted without changing terrain."}},
       {720, 20}}};
  graph.links = {
      {"shape_to_appearance", {"shape", "field"}, {"appearance", "field"}},
      {"appearance_to_output",
       {"appearance", "field"},
       {"terrain_output", "field"}}};
  preset.graph = graph.toJson();
  return preset;
}
} // namespace

const std::vector<TerrainPreset> &builtinTerrainPresets() {
  static const std::vector<TerrainPreset> presets{
      starter("flat-building-site", "Flat Building Site",
              "A level surface for learning placement and building a colony.",
              0, 32, 0, {.48F, .38F, .27F, 1}),
      starter("desert-dunes", "Desert Dunes",
              "Soft sandy relief for a dry planet. Flatten a landing area with "
              "the viewport brush.",
              5, 40, .2F, {.65F, .32F, .14F, 1}),
      starter("rolling-hills", "Rolling Hills",
              "Gentle green hills for learning seeded noise and terrain scale.",
              8, 48, .35F, {.25F, .46F, .16F, 1}),
      starter("mountain-range", "Mountain Range",
              "Tall, rough rocky terrain. Reduce Height variation for easier "
              "building sites.",
              30, 56, .7F, {.4F, .38F, .36F, 1})};
  return presets;
}
} // namespace demi::runtime
