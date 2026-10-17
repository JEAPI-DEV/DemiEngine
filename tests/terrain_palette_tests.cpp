#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAssetContent.h"
#include "demi/assets/DataDocument.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainPalette.h"
#include "demi/runtime/terrain/TerrainScatter.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

int main() {
  using namespace demi;
  using namespace demi::runtime;
  using Json = nlohmann::json;
  const auto root =
      std::filesystem::temp_directory_path() / "demi-palette-v2-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  const auto write = [&](const char *name, const Json &value) {
    std::ofstream(root / name) << value.dump(2);
  };
  write("surface.json", {{"format_version", 1},
                         {"parameters", {{"base_color", {0.2, 0.4, 0.1, 1}}}}});
  write("set.json", {{"format_version", 1},
                     {"name", "Ground"},
                     {"roles", {{"ground", "asset://surface"}}}});
  AssetRegistry registry;
  registry.projectDirectory = root;
  registry.assets = {
      {.id = "asset://mesh", .type = "Model3D", .sourceHash = "mesh"},
      {.id = "asset://set",
       .type = "DataAsset",
       .sourceHash = "set",
       .settingsJson = R"({"content_type":"terrain_material_set"})",
       .sourcePath = root / "set.json"},
      {.id = "asset://surface",
       .type = "Material",
       .sourceHash = "surface",
       .sourcePath = root / "surface.json"}};
  const auto parse = [&](const Json &value) {
    const auto document = assets::parseDataDocument(value.dump());
    assert(document.document);
    return parseTerrainPalette(*document.document, registry, "asset://palette");
  };
  const auto rejects = [&](const Json &value, const std::string &message) {
    try {
      (void)parse(value);
      assert(false && "Invalid palette was accepted");
    } catch (const std::invalid_argument &failure) {
      assert(std::string(failure.what()).find(message) != std::string::npos);
    }
  };
  const Json surfaces{{"format_version", 3},
                      {"name", "Surface only"},
                      {"material_set", "asset://set"}};
  const auto surfacePalette = parse(surfaces);
  assert(surfacePalette.placements.empty());
  assert(surfacePalette.materialSet == "asset://set");
  assert(surfacePalette.assetDependencies() ==
         std::vector<std::string>{"asset://set"});
  auto recipe = TerrainRecipe::parse(TerrainRecipe::defaults());
  recipe.cellsX = recipe.cellsZ = 8;
  recipe.chunkCells = 8;
  const auto field = TerrainGenerator::generate(recipe);
  assert(field &&
         scatterTerrain(*field, recipe, surfacePalette).placements.empty());

  Json mixed = surfaces;
  mixed["placements"] = {
      {"pine_sparse",
       {{"model", "asset://mesh"},
        {"spacing", 12},
        {"density", 0.15},
        {"candidate_spacing", 1},
        {"collision", "none"},
        {"scale", {0.8, 1.4}},
        {"biomes", {"meadow", "meadow"}}}},
      {"pine_dense",
       {{"model", "asset://mesh"}, {"spacing", 2}, {"collision", "none"}}},
      {"decorated_tree", {{"prefab", "prefab://tree"}}}};
  mixed["art_density"] = 0.8;
  mixed["grass_density"] = 0.5;
  mixed["tree_density"] = 1.2;
  mixed["rock_density"] = 0.7;
  auto &pine = mixed["placements"]["pine_sparse"];
  pine["category"] = "tree";
  pine["slope"] = {5, 30};
  pine["moisture"] = {0.2, 0.8};
  pine["water_distance"] = {3, 20};
  pine["yaw"] = {-90, 90};
  pine["cluster"] = {{"size", 24}, {"strength", 0.6}};
  const auto palette = parse(mixed);
  const auto &parsedPine = palette.placements.at("pine_sparse");
  assert(parsedPine.category == TerrainScatterCategory::Tree);
  assert(parsedPine.slope.minimum == 5 && parsedPine.slope.maximum == 30);
  assert(parsedPine.waterDistance.minimum == 3 &&
         parsedPine.waterDistance.maximum == 20);
  assert(parsedPine.yawDegrees.minimum == -90 &&
         parsedPine.yawDegrees.maximum == 90);
  assert(parsedPine.clusterSize == 24 && parsedPine.clusterStrength == 0.6F);
  assert(palette.grassDensity == 0.5F && palette.rockDensity == 0.7F);
  assert(palette.placements.size() == 3);
  assert(palette.placements.at("pine_sparse").spacing == 12);
  assert(palette.placements.at("pine_dense").spacing == 2);
  assert(palette.placements.at("pine_sparse").biomes ==
         std::vector<std::string>{"meadow"});
  assert(palette.placements.at("decorated_tree").model.empty());
  assert(palette.assetDependencies() ==
         (std::vector<std::string>{"asset://mesh", "asset://set",
                                   "prefab://tree"}));
  const auto content = assets::inspectDataAssetContent(
      "terrain_palette", *assets::parseDataDocument(mixed.dump()).document,
      registry, "asset://palette");
  assert(content.diagnostics.empty());
  assert(content.dependencies ==
         (std::vector<std::string>{"asset://mesh", "asset://set"}));
  auto invalid = mixed;
  invalid["format_version"] = 1;
  rejects(invalid, "Migrate");
  invalid = mixed;
  invalid["roles"] = Json::object();
  rejects(invalid, "unknown field");
  invalid = mixed;
  invalid["placements"]["bad/id"] = {{"model", "asset://mesh"}};
  rejects(invalid, "rule IDs");
  invalid = mixed;
  invalid["placements"]["pine_sparse"]["prefab"] = "prefab://tree";
  rejects(invalid, "exactly one");
  invalid = mixed;
  invalid["placements"]["pine_sparse"].erase("model");
  rejects(invalid, "exactly one");
  invalid = mixed;
  invalid["placements"]["pine_sparse"]["model"] = "asset://surface";
  rejects(invalid, "Model3D");
  invalid = mixed;
  invalid["material_set"] = "asset://mesh";
  rejects(invalid, "material set");
  const Json invalidFields{{"weight", 1},
                           {"density", -1},
                           {"candidate_spacing", 0},
                           {"category", "forest"},
                           {"slope", {-1, 20}},
                           {"moisture", {0.8, 0.2}},
                           {"water_distance", {-1, 30}},
                           {"yaw", {-400, 90}},
                           {"cluster", {{"strength", 2}}},
                           {"spacing", -1},
                           {"scale", {2, 1}},
                           {"lod", -1},
                           {"collision", "dynamic"}};
  for (const auto &[key, value] : invalidFields.items()) {
    invalid = mixed;
    invalid["placements"]["pine_sparse"][key] = value;
    rejects(invalid, key);
  }
  invalid = mixed;
  invalid["placements"]["pine_sparse"]["candidate_spacing"] = 100;
  rejects(invalid, "candidate capacity");
  invalid = mixed;
  invalid["placements"]["pine_sparse"]["collision"] = "static";
  rejects(invalid, "prefab with authored collider");
  invalid = mixed;
  invalid["art_density"] = -1;
  rejects(invalid, "art_density");
  invalid = {
      {"format_version", 3}, {"name", "Empty"}, {"placements", Json::object()}};
  rejects(invalid, "must supply");
  assert(terrainPlacementRuleHash("pine_sparse") == 13048787414137992044ULL);
  assert(terrainPlacementRuleHash("pine_sparse") !=
         terrainPlacementRuleHash("pine_dense"));
  std::filesystem::remove_all(root);
}
