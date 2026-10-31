#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAssetContent.h"
#include "demi/assets/DataDocument.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/assets/TerrainRegionAsset.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using namespace demi;
using namespace demi::assets;

std::shared_ptr<const DataDocument> document(const nlohmann::json &value) {
  const auto result = parseDataDocument(value.dump());
  assert(result.document);
  return result.document;
}

void authoredGraphReferences() {
  const auto project =
      std::filesystem::path(DEMI_SOURCE_DIR) / "examples/terrain_3d";
  const auto registry = loadAssetRegistry(project);
  assert(!hasErrors(registry.diagnostics));
  const auto *terrain =
      findAsset(registry, "asset://terrain/regions/alpine_terrain");
  assert(terrain);
  const auto source = loadTerrainAssetSource(*terrain);
  const auto config =
      loadTerrainRegionSource(registry, "asset://terrain/regions/alpine");
  assert(config.recipe.toJson() ==
         runtime::TerrainRecipe::parse(source.recipe).toJson());
  const runtime::RegionalTerrainSampler sampler(config);
  const double x = config.originX + config.recipe.size.x * .4;
  const double z = config.originZ + config.recipe.size.y * .5;
  auto changed = config;
  for (auto &node : changed.recipe.graph["nodes"]) {
    if (node["type"] == "noise") {
      node["parameters"]["base_height"] =
          node["parameters"].value("base_height", 0.0) + 500.0;
    }
  }
  const runtime::RegionalTerrainSampler edited(changed);
  assert(edited.sampleHeight(x, z) > sampler.sampleHeight(x, z) + 499.0F);
}

void wrapperValidation() {
  nlohmann::json source{
      {"format_version", 1},
      {"terrain", "asset://terrain/test"},
      {"origin", {-20, 40}},
      {"sampling", {{"base_spacing", 2}, {"normal_spacing", 1}}}};
  const auto parsed = parseTerrainRegionAsset(*document(source));
  assert(parsed.terrain == "asset://terrain/test" && parsed.originX == -20);
  const auto reject = [](const auto &value) {
    bool failed = false;
    try {
      (void)parseTerrainRegionAsset(*document(value));
    } catch (const std::invalid_argument &) {
      failed = true;
    }
    assert(failed);
  };
  auto layered = source;
  layered["streaming"] = {{"vegetation_radius", 256},
                           {"vegetation_canopy_radius", 3072},
                           {"vegetation_canopy_cell_samples", 512}};
  const auto tiers = parseTerrainRegionAsset(*document(layered));
  assert(tiers.streaming.vegetationRadius == 256);
  assert(tiers.streaming.vegetationCanopyRadius == 3072);
  assert(tiers.streaming.vegetationCanopyCellSamples == 512);
  layered["streaming"]["vegetation_canopy_radius"] = -1;
  reject(layered);
  layered["streaming"]["vegetation_canopy_radius"] = 100;
  layered["streaming"]["vegetation_canopy_cell_samples"] = 0;
  reject(layered);
  auto invalid = source;
  invalid["sampling"]["base_spacing"] = 0;
  reject(invalid);
  invalid = source;
  invalid["layers"] = nlohmann::json::array();
  reject(invalid);
  invalid = source;
  invalid.erase("terrain");
  reject(invalid);
  invalid = source;
  invalid["origin"] = {1};
  reject(invalid);
  invalid = source;
  invalid["format_version"] = 2;
  reject(invalid);
  AssetRegistry registry;
  registry.assets.push_back({.id = "asset://terrain/test", .type = "Material"});
  assert(hasErrors(inspectDataAssetContent("terrain_region", *document(source),
                                           registry, "asset://region")
                       .diagnostics));
  registry.assets.front().type = "Terrain";
  const auto content = inspectDataAssetContent(
      "terrain_region", *document(source), registry, "asset://region");
  assert(!hasErrors(content.diagnostics));
  assert(content.dependencies ==
         std::vector<std::string>{"asset://terrain/test"});
}
} // namespace
int main() {
  wrapperValidation();
  authoredGraphReferences();
  std::cout << "Regional terrain asset checks passed\n";
}
