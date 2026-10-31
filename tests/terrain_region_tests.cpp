#include "demi/runtime/terrain/RegionalTerrainSampler.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainGraph.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace demi::runtime;
void check(bool value, const char *message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}
bool same(float a, float b) {
  return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}
TerrainRegionConfig fixture(bool connectedGraph = true) {
  TerrainRegionConfig config;
  config.originX = -64;
  config.originZ = -48;
  config.baseSampleSpacing = 1;
  config.normalSampleSpacing = .5;
  auto &recipe = config.recipe;
  recipe.seed = 31;
  recipe.size = {128, 96};
  recipe.cellsX = 128;
  recipe.cellsZ = 96;
  recipe.chunkCells = 16;
  recipe.biomes.at(recipe.defaultBiome).color = {.2F, .4F, .1F, 1};
  recipe.biomes.at(recipe.defaultBiome).material = "asset://test/ground";
  recipe.biomes.at(recipe.defaultBiome).textureScale = .5F;
  auto &shape = recipe.landforms.at(recipe.defaultLandform);
  shape.baseHeight = 12;
  shape.heightVariation = 2;
  shape.featureSize = 20;
  if (!connectedGraph) {
    return config;
  }
  TerrainGraph graph;
  graph.output = "output";
  graph.nodes = {
      {"ground",
       "noise",
       {{"base_height", 12},
        {"height_variation", 2},
        {"feature_size", 20},
        {"warp_amplitude", 2},
        {"warp_feature_size", 30},
        {"heading", 23},
        {"stretch", 1.5}}},
      {"ridge",
       "noise",
       {{"base_height", 27},
        {"height_variation", 12},
        {"feature_size", 16},
        {"profile", "ridged"},
        {"seed_offset", 7}}},
      {"path",
       "path_distance_mask",
       {{"points", {{0, 45}, {64, 51}, {128, 46}}},
        {"width", 8},
        {"falloff", 16}}},
      {"blend", "height_blend", {{"amount", .75}}},
      {"elevation",
       "elevation_mask",
       {{"minimum", 25}, {"maximum", 50}, {"falloff", 3}}},
      {"cut", "offset", {{"amount", -3}}},
      {"polygon",
       "polygon_mask",
       {{"points", {{45, 35}, {85, 35}, {85, 65}, {45, 65}}}, {"falloff", 5}}},
      {"detail", "offset", {{"amount", -4}}},
      {"biomes", "biomes"},
      {"output", "output"}};
  graph.links = {
      {"ground_path", {"ground", "field"}, {"path", "field"}},
      {"ground_blend", {"ground", "field"}, {"blend", "a"}},
      {"ridge_blend", {"ridge", "field"}, {"blend", "b"}},
      {"path_blend", {"path", "mask"}, {"blend", "mask"}},
      {"blend_elevation", {"blend", "field"}, {"elevation", "field"}},
      {"blend_cut", {"blend", "field"}, {"cut", "field"}},
      {"elevation_cut", {"elevation", "mask"}, {"cut", "mask"}},
      {"cut_polygon", {"cut", "field"}, {"polygon", "field"}},
      {"cut_detail", {"cut", "field"}, {"detail", "field"}},
      {"polygon_detail", {"polygon", "mask"}, {"detail", "mask"}},
      {"detail_biomes", {"detail", "field"}, {"biomes", "field"}},
      {"biomes_output", {"biomes", "field"}, {"output", "field"}}};
  recipe.graph = graph.toJson();
  return config;
}

void equalSample(const HeightField &a, int ax, int az, const HeightField &b,
                 int bx, int bz) {
  check(same(a.height(ax, az), b.height(bx, bz)), "Shared height differs");
  const auto an = a.normal(ax, az);
  const auto bn = b.normal(bx, bz);
  check(same(an.x, bn.x) && same(an.y, bn.y) && same(an.z, bn.z),
        "Shared fixed-footprint normal differs");
}

template <class Operation>
void rejected(Operation operation, std::string_view expected = {}) {
  bool failed = false;
  try {
    operation();
  } catch (const std::invalid_argument &error) {
    failed =
        expected.empty() ||
        std::string_view(error.what()).find(expected) != std::string_view::npos;
  }
  check(failed,
        "Invalid regional request was accepted or lacked its diagnostic");
}

void finiteGraphParity() {
  for (bool connected : {false, true}) {
    const auto config = fixture(connected);
    const auto finite = TerrainGenerator::generate(config.recipe);
    check(finite.has_value(), "Finite terrain fixture failed");
    RegionalTerrainSampler sampler(config);
    for (int z = 0; z <= finite->cellsZ; ++z) {
      for (int x = 0; x <= finite->cellsX; ++x) {
        const auto point = finite->position(x, z);
        check(same(finite->height(x, z),
                   sampler.sampleHeight(config.originX + point.x,
                                        config.originZ + point.y)),
              "Regional graph differs from canonical finite graph semantics");
      }
    }
  }
  auto config = fixture(false);
  config.recipe.graph = defaultTerrainGraph();
  const auto finite = TerrainGenerator::generate(config.recipe);
  check(finite.has_value(), "Finite landform fixture failed");
  check(same(finite->height(10, 10),
             RegionalTerrainSampler(config).sampleHeight(-54, -38)),
        "Landform graph did not reuse the canonical evaluator");
  auto graph = TerrainGraph::parse(config.recipe.graph);
  graph.nodes.front().type = "constant";
  graph.nodes.front().parameters = {{"height", 42}};
  config.recipe.graph = graph.toJson();
  check(RegionalTerrainSampler(config).sampleHeight(0, 0) == 42,
        "Constant graph source ignored its authored value");
}

void tileContinuityAndOrder() {
  const auto config = fixture();
  RegionalTerrainSampler sampler(config);
  const TerrainRegionTileRequest request{.firstSampleX = 32,
                                         .firstSampleZ = 24,
                                         .cellsX = 16,
                                         .cellsZ = 16,
                                         .stride = 1};
  auto eastRequest = request;
  eastRequest.firstSampleX += request.cellsX;
  const auto east = sampler.sampleTile(eastRequest);
  const auto centre = sampler.sampleTile(request);
  const auto repeated = sampler.sampleTile(request);
  check(centre.originX == -32 && centre.originZ == -24, "Tile origin differs");
  check(centre.field.projectionOrigin.x ==
                float(centre.originX - config.originX) &&
            centre.field.projectionOrigin.y ==
                float(centre.originZ - config.originZ),
        "Regional material projection lost its source coordinate frame");
  check(centre.field.biomeMaterials.front() == "asset://test/ground" &&
            centre.field.biomeTextureScales.front() == .5F &&
            centre.field.biomeColors.front().g == .4F,
        "Recipe surface metadata was lost");
  for (int z = 0; z <= 16; ++z) {
    equalSample(centre.field, 16, z, east.field, 0, z);
    for (int x = 0; x <= 16; ++x) {
      equalSample(centre.field, x, z, repeated.field, x, z);
    }
  }
  auto northRequest = request;
  northRequest.firstSampleZ += request.cellsZ;
  const auto north = sampler.sampleTile(northRequest);
  for (int x = 0; x <= 16; ++x) {
    equalSample(centre.field, x, 16, north.field, x, 0);
  }
  auto coarseRequest = request;
  coarseRequest.cellsX = coarseRequest.cellsZ = 8;
  coarseRequest.stride = 2;
  const auto coarse = sampler.sampleTile(coarseRequest);
  for (int z = 0; z <= 8; ++z) {
    for (int x = 0; x <= 8; ++x) {
      equalSample(centre.field, x * 2, z * 2, coarse.field, x, z);
    }
  }
  const auto edge = sampler.sampleTile({.firstSampleX = 112,
                                        .firstSampleZ = 80,
                                        .cellsX = 16,
                                        .cellsZ = 16,
                                        .stride = 1});
  check(same(edge.field.height(16, 16), sampler.sampleHeight(64, 48)),
        "Outer edge differs");
  check(same(edge.field.normal(16, 16).x, sampler.sampleNormal(64, 48).x),
        "Outer edge did not use the global normal halo");
}

void authoringIdentityAndCoordinates() {
  auto config = fixture();
  RegionalTerrainSampler sampler(config);
  ++config.recipe.seed;
  RegionalTerrainSampler other(config);
  bool changed = false;
  for (int z = -48; z <= 48; z += 8) {
    for (int x = -64; x <= 64; x += 8) {
      changed |= !same(sampler.sampleHeight(x, z), other.sampleHeight(x, z));
    }
  }
  check(changed, "Recipe seed did not change graph output");
  config = fixture();
  auto graph = TerrainGraph::parse(config.recipe.graph);
  std::ranges::reverse(graph.nodes);
  std::ranges::reverse(graph.links);
  config.recipe.graph = graph.toJson();
  check(same(sampler.sampleHeight(-40, 30),
             RegionalTerrainSampler(config).sampleHeight(-40, 30)),
        "Graph source order changed compiled sampling");
  const auto detail =
      std::ranges::find(graph.nodes, "detail", &TerrainGraphNode::id);
  check(detail != graph.nodes.end(), "Authored detail node is missing");
  detail->parameters["amount"] = -14;
  config.recipe.graph = graph.toJson();
  check(!same(sampler.sampleHeight(0, 0),
              RegionalTerrainSampler(config).sampleHeight(0, 0)),
        "Authored graph tuning did not change terrain");
  config = fixture();
  config.originX += 1000000000;
  config.originZ -= 1000000000;
  check(same(sampler.sampleHeight(-40, 30),
             RegionalTerrainSampler(config).sampleHeight(1000000000 - 40,
                                                         -1000000000 + 30)),
        "Large origin changed region-local graph coordinates");
}

void regionalBiomeRules() {
  auto config = fixture();
  config.recipe.biomes["summit"] =
      config.recipe.biomes.at(config.recipe.defaultBiome);
  TerrainBiomeRule rule;
  rule.id = "high_ground";
  rule.biome = "summit";
  rule.elevation = {true, -100000, 100000};
  config.recipe.rules.push_back(rule);
  RegionalTerrainSampler sampler(config);
  const auto tile = sampler.sampleTile({0, 0, 8, 8, 1});
  for (std::size_t i = 0; i < tile.field.biomeIndices.size(); ++i) {
    check(tile.field.biomeIds.at(tile.field.biomeIndices.at(i)) == "summit",
          "Regional final Biomes node did not apply the shared rule");
  }
  config.recipe.rules.front().moisture = {true, 0, .5F};
  rejected([&] { RegionalTerrainSampler invalid(config); },
           "environment measurements");
}

void unsupportedFeaturesAndBounds() {
  RegionalTerrainSampler sampler(fixture());
  rejected([&] { (void)sampler.sampleTile({}); });
  rejected([&] {
    (void)sampler.sampleTile(
        {.firstSampleX = -1, .cellsX = 1, .cellsZ = 1, .stride = 1});
  });
  rejected([&] {
    (void)sampler.sampleTile(
        {.firstSampleX = 128, .cellsX = 1, .cellsZ = 1, .stride = 1});
  });
  rejected([&] {
    (void)sampler.sampleTile(
        {.firstSampleX = std::numeric_limits<std::int64_t>::max(),
         .cellsX = 1,
         .cellsZ = 1,
         .stride = 1});
  });
  rejected([&] {
    (void)sampler.sampleHeight(std::numeric_limits<double>::infinity(), 0);
  });
  rejected([&] { (void)sampler.sampleNormal(-65, 0); });
  check(std::isfinite(sampler.sampleHeight(-64.5, 0)),
        "Normal halo unavailable");
  rejected([] { RegionalTerrainSampler invalid(TerrainRegionConfig{}); });
  auto config = fixture();
  auto graph = TerrainGraph::parse(config.recipe.graph);
  graph.nodes.push_back({"regional_erosion", "erosion"});
  graph.links.push_back(
      {"erosion_input", {"detail", "field"}, {"regional_erosion", "field"}});
  auto output =
      std::ranges::find(graph.links, "biomes_output", &TerrainGraphLink::id);
  output->from = {"regional_erosion", "field"};
  config.recipe.graph = graph.toJson();
  rejected([&] { RegionalTerrainSampler invalid(config); },
           "Node regional_erosion");
  graph = TerrainGraph::parse(fixture().recipe.graph);
  graph.nodes.push_back({"scatter", "scatter"});
  graph.links.push_back(
      {"scatter_input", {"detail", "field"}, {"scatter", "field"}});
  graph.links.push_back(
      {"scatter_output", {"scatter", "instances"}, {"output", "instances"}});
  config.recipe.graph = graph.toJson();
  rejected([&] { RegionalTerrainSampler invalid(config); }, "port instances");
}
} // namespace

int main() {
  auto subprecision = fixture();
  subprecision.recipe.size.x = 240000;
  subprecision.normalSampleSpacing = 0.000001;
  rejected([&] { RegionalTerrainSampler invalid(subprecision); }, "precision");
  finiteGraphParity();
  tileContinuityAndOrder();
  authoringIdentityAndCoordinates();
  unsupportedFeaturesAndBounds();
  regionalBiomeRules();
  std::cout << "Regional terrain graph sampling checks passed\n";
}
