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

TerrainPreset withPolygonRegion(TerrainPreset preset,
                                TerrainGraphNode regionSurface,
                                nlohmann::json points, float falloff,
                                std::string explanation) {
  auto graph = TerrainGraph::parse(preset.graph);
  regionSurface.id = "region_surface";
  regionSurface.position = {40, 680};
  graph.nodes.push_back(std::move(regionSurface));
  graph.nodes.push_back({"region_mask",
                         "polygon_mask",
                         {{"points", std::move(points)}, {"falloff", falloff}},
                         {380, 320}});
  graph.nodes.push_back(
      {"region_blend", "height_blend", {{"amount", 1}}, {720, 320}});
  graph.nodes.push_back({"region_help",
                         "comment",
                         {{"text", std::move(explanation)}},
                         {380, 20}});
  graph.nodes.push_back(
      {"blend_help",
       "comment",
       {{"text",
         "Height Blend uses the polygon mask to replace the background "
         "with Region Surface. Amount 1 reaches the full region height. "
         "Falloff softens the outside edge without changing the interior."}},
       {720, 20}});
  for (auto &node : graph.nodes) {
    if (node.id == "appearance" || node.id == "appearance_help") {
      node.position.x = 1060;
    }
    if (node.id == "terrain_output" || node.id == "output_help") {
      node.position.x = 1400;
    }
  }
  graph.links = {
      {"background_to_mask", {"shape", "field"}, {"region_mask", "field"}},
      {"background_to_blend", {"shape", "field"}, {"region_blend", "a"}},
      {"region_to_blend", {"region_surface", "field"}, {"region_blend", "b"}},
      {"mask_to_blend", {"region_mask", "mask"}, {"region_blend", "mask"}},
      {"blend_to_appearance",
       {"region_blend", "field"},
       {"appearance", "field"}},
      {"appearance_to_output",
       {"appearance", "field"},
       {"terrain_output", "field"}}};
  preset.graph = graph.toJson();
  return preset;
}

TerrainPreset basinStarter() {
  auto preset = starter(
      "basin", "Basin",
      "A low, level basin enclosed by rolling terrain. Change the polygon to "
      "move or reshape the sheltered floor.",
      8, 52, .25F, {.35F, .46F, .25F, 1});
  preset.graph["nodes"][0]["parameters"]["base_height"] = 18;
  return withPolygonRegion(
      std::move(preset), {"", "constant", {{"height", 2}}, {}},
      {{32, 40}, {52, 28}, {88, 32}, {102, 62}, {88, 98}, {42, 96}, {26, 68}},
      16,
      "Polygon points use terrain-local X/Z world units on this 128-by-128 "
      "landscape. Inside the polygon, Region Surface supplies the 2-unit basin "
      "floor. A 16-unit outside falloff blends back into the surrounding "
      "hills.");
}

TerrainPreset plateauStarter() {
  auto preset = starter("plateau", "Plateau",
                        "An elevated, level building area above gentle "
                        "lowlands. Edit the polygon "
                        "for an irregular mesa or a wider usable platform.",
                        3, 48, .25F, {.48F, .4F, .29F, 1});
  preset.graph["nodes"][0]["parameters"]["base_height"] = 2;
  return withPolygonRegion(
      std::move(preset), {"", "constant", {{"height", 20}}, {}},
      {{34, 36}, {80, 26}, {100, 50}, {92, 92}, {58, 104}, {28, 80}}, 14,
      "Region Surface sets the plateau's elevation to 20 units. The polygon "
      "defines its level top; the 14-unit outside falloff forms the "
      "escarpment. "
      "The surrounding lowlands keep their seeded noise.");
}

TerrainPreset coastalIslandStarter() {
  auto preset = starter(
      "coastal-island", "Coastal Island",
      "An irregular green island in a shallow sea. Shape its coast by moving "
      "polygon points; change Region Surface to adjust inland relief.",
      0, 32, 0, {.3F, .48F, .22F, 1});
  preset.graph["nodes"][0]["parameters"]["height"] = -6;
  preset = withPolygonRegion(
      std::move(preset),
      {"",
       "noise",
       {{"base_height", 14},
        {"height_variation", 8},
        {"feature_size", 32},
        {"roughness", .3}},
       {}},
      {{40, 28}, {76, 24}, {96, 48}, {88, 78}, {66, 100}, {34, 88}, {24, 58}},
      18,
      "The background is a -6-unit sea bed. The polygon selects the island's "
      "interior, with an 18-unit outside falloff forming its coast. Ocean "
      "water "
      "stays at elevation zero; moving the polygon changes land, not sea "
      "level.");
  preset.biomes["shore"].color = {.7F, .62F, .4F, 1};
  TerrainBiomeRule shore;
  shore.id = "shore";
  shore.biome = "shore";
  shore.elevation = {true, -6, 3};
  preset.rules.push_back(shore);
  auto graph = TerrainGraph::parse(preset.graph);
  graph.nodes.push_back({"ocean",
                         "water",
                         {{"kind", "ocean"}, {"level", 0}, {"radius", 0}},
                         {1060, 680}});
  for (auto &node : graph.nodes) {
    if (node.id == "shape_help") {
      node.parameters["text"] =
          "The background sets the sea bed to -6 units. Keep it below the "
          "Ocean node's zero-unit water level. The separate Region Surface "
          "noise shapes the island above it.";
    }
  }
  for (auto &link : graph.links) {
    if (link.id == "blend_to_appearance") {
      link.to = {"ocean", "field"};
    }
  }
  graph.links.push_back(
      {"ocean_to_appearance", {"ocean", "field"}, {"appearance", "field"}});
  graph.links.push_back(
      {"ocean_to_output", {"ocean", "water"}, {"terrain_output", "water"}});
  preset.graph = graph.toJson();
  return preset;
}
TerrainPreset drainageRiverStarter() {
  auto preset = starter(
      "drainage-river", "Drainage River",
      "A drainage-connected river descending across a gently sloping landscape "
      "toward the sea. Move the source and inspect its endpoint diagnostic.",
      3, 32, .25F, {.3F, .46F, .24F, 1});
  TerrainGraph graph;
  graph.output = "terrain_output";
  graph.nodes = {
      {"shape",
       "noise",
       {{"base_height", -4},
        {"height_variation", 3},
        {"feature_size", 32},
        {"roughness", .25}},
       {40, 320}},
      {"slope_mask",
       "path_distance_mask",
       {{"points", {{0, 0}, {128, 0}}}, {"width", 0}, {"falloff", 128}},
       {380, 320}},
      {"sloping_valley", "offset", {{"amount", 30}}, {720, 320}},
      {"ocean",
       "water",
       {{"kind", "ocean"}, {"level", 0}, {"radius", 0}},
       {1060, 320}},
      {"drainage", "drainage", {{"sea_level", 0}}, {1400, 320}},
      {"river",
       "river_route",
       {{"source_x", 64},
        {"source_z", 32},
        {"channel_width", 8},
        {"channel_depth", 2},
        {"water_depth", 1}},
       {1740, 320}},
      {"appearance", "biomes", nlohmann::json::object(), {2080, 320}},
      {"terrain_output", "output", nlohmann::json::object(), {2420, 320}},
      {"shape_help",
       "comment",
       {{"text",
         "Seeded noise supplies modest local relief above a -4-unit sea "
         "bed. Height variation and feature size shape those details; "
         "the separate masked offset establishes the main drainage slope."}},
       {40, 20}},
      {"slope_help",
       "comment",
       {{"text", "The terrain is 128-by-128 world units. The path mask fades "
                 "away from Z=0; Offset raises that side by 30 units, creating "
                 "a broad downhill slope toward the negative-height sea bed."}},
       {380, 20}},
      {"river_help",
       "comment",
       {{"text",
         "Source X/Z selects a terrain-local drainage sample. River Route "
         "follows nonascending terrain triangle edges and carves its bed. "
         "Its diagnostic reports sea level, field edge, closed basin or "
         "blocked surface. A closed basin never invents a spill route."}},
       {1740, 20}},
      {"water_help",
       "comment",
       {{"text",
         "Ocean supplies the existing zero-level sea before river routing. "
         "River water follows the carved bed and joins that sea. These are "
         "generated surfaces and gameplay "
         "queries, not a fluid simulation. Keep the water links connected "
         "so Terrain Output publishes both bodies."}},
       {2080, 20}}};
  graph.links = {
      {"shape_to_mask", {"shape", "field"}, {"slope_mask", "field"}},
      {"shape_to_offset", {"shape", "field"}, {"sloping_valley", "field"}},
      {"mask_to_offset", {"slope_mask", "mask"}, {"sloping_valley", "mask"}},
      {"slope_to_ocean", {"sloping_valley", "field"}, {"ocean", "field"}},
      {"ocean_to_drainage", {"ocean", "field"}, {"drainage", "field"}},
      {"drainage_to_river", {"drainage", "drainage"}, {"river", "drainage"}},
      {"ocean_to_river_water", {"ocean", "water"}, {"river", "water"}},
      {"river_to_appearance", {"river", "field"}, {"appearance", "field"}},
      {"appearance_to_output",
       {"appearance", "field"},
       {"terrain_output", "field"}},
      {"river_to_output", {"river", "water"}, {"terrain_output", "water"}}};
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
              30, 56, .7F, {.4F, .38F, .36F, 1}),
      basinStarter(),
      plateauStarter(),
      coastalIslandStarter(),
      drainageRiverStarter()};
  return presets;
}
} // namespace demi::runtime
