#include "demi/runtime/terrain/TerrainCook.h"
#include "demi/runtime/terrain/TerrainDrainage.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainGraphRegistry.h"
#include "demi/runtime/terrain/TerrainLandform.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "demi/runtime/terrain/TerrainUpdate.h"

#include <algorithm>
#include <cmath>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace demi::runtime;

namespace {

nlohmann::json noiseGraph(const nlohmann::json &parameters) {
  auto graph = nlohmann::json::parse(R"({
    "format_version": 1,
    "output": "terrain_output",
    "nodes": [
      {"id": "source", "type": "noise"},
      {"id": "terrain_output", "type": "output"}
    ],
    "links": [{
      "id": "source_output",
      "from": {"node": "source", "port": "field"},
      "to": {"node": "terrain_output", "port": "field"}
    }]
  })");
  graph["nodes"][0]["parameters"] = parameters;
  return graph;
}

TerrainRecipe smallRecipe() {
  TerrainRecipe recipe;
  recipe.size = {16, 16};
  recipe.cellsX = 16;
  recipe.cellsZ = 16;
  recipe.chunkCells = 8;
  return recipe;
}

bool rejected(const nlohmann::json &graph) {
  try {
    (void)TerrainGraph::parse(graph);
    return false;
  } catch (const std::invalid_argument &) {
    return true;
  }
}

void metadataUsesCanonicalNativeDefaults() {
  const auto *noise = terrainGraphNodeDefinition("noise");
  assert(noise);
  nlohmann::json settings = nlohmann::json::object();
  for (const auto &parameter : noise->parameters) {
    if (parameter.name != "seed_offset") {
      settings[parameter.name] = parameter.defaultValue;
    }
  }
  assert(TerrainLandform::parse(settings) == TerrainLandform{});
  const auto profile = std::ranges::find(noise->parameters, "profile",
                                         &TerrainGraphParameter::name);
  assert(profile != noise->parameters.end());
  assert(profile->kind == TerrainGraphParameterKind::Choice);
  assert(profile->choices.size() == TerrainLandform::profileNames.size());
  for (std::size_t index = 0; index < profile->choices.size(); ++index) {
    assert(profile->choices[index] == TerrainLandform::profileNames[index]);
  }
}

void graphNoiseUsesTheSharedLandformEvaluator() {
  const nlohmann::json parameters{
      {"profile", "ridged"}, {"base_height", 3},        {"height_variation", 9},
      {"feature_size", 24},  {"roughness", 0.4},        {"octaves", 3},
      {"warp_amplitude", 4}, {"warp_feature_size", 48}, {"stretch", 2},
      {"heading", 35},       {"seed_offset", 7}};
  auto authored = smallRecipe();
  authored.graph = noiseGraph(parameters);
  const auto graphField = executeTerrainGraph(authored);
  assert(graphField);

  auto native = smallRecipe();
  auto landformParameters = parameters;
  landformParameters.erase("seed_offset");
  native.landforms = {{"graph", TerrainLandform::parse(landformParameters)}};
  native.defaultLandform = "graph";
  for (auto &[id, biome] : native.biomes) {
    biome.landform = "graph";
  }
  native.seed += 7;
  const auto nativeField = TerrainGenerator::generateBase(native);
  assert(nativeField);
  assert(graphField->heights == nativeField->heights);
  assert(graphField->baseHeights == nativeField->baseHeights);
}

void noiseParameterDomainsAreValidatedBeforeExecution() {
  for (const auto &parameters :
       {nlohmann::json{{"profile", "unsupported"}},
        nlohmann::json{{"warp_amplitude", -1}},
        nlohmann::json{{"warp_feature_size", 0}},
        nlohmann::json{{"stretch", 0}}, nlohmann::json{{"stretch", -1}},
        nlohmann::json{{"heading", "north"}},
        nlohmann::json{{"heading", std::numeric_limits<double>::infinity()}},
        nlohmann::json{{"warp_amplitude", 1.0e100}}}) {
    assert(rejected(noiseGraph(parameters)));
  }
  assert(!rejected(noiseGraph({{"heading", -450}, {"stretch", 0.5}})));
}

void landformControlsInvalidateTheGraphSourceCache() {
  auto authored = smallRecipe();
  authored.graph = defaultTerrainGraph();
  const auto first = executeTerrainGraph(authored);
  assert(first);
  TerrainGraphInputs retained;
  retained.previousCache = first->graphArtifacts->cache;
  authored.landforms.at("default").warpAmplitude = 5;
  const auto warped = executeTerrainGraph(authored, retained);
  assert(warped);
  const auto source = std::ranges::find(warped->graphArtifacts->nodes,
                                        "landform", &TerrainGraphNodeRun::id);
  assert(source != warped->graphArtifacts->nodes.end());
  assert(!source->cached);
  assert(warped->heights != first->heights);
}

void pathMetadataAndParserShareTheCoordinateContract() {
  const auto *definition = terrainGraphNodeDefinition("path_distance_mask");
  assert(definition);
  assert(definition->inputs.size() == 1 && definition->inputs[0].required);
  nlohmann::json defaults = nlohmann::json::object();
  for (const auto &parameter : definition->parameters) {
    defaults[parameter.name] = parameter.defaultValue;
  }
  assert(defaults["points"] == nlohmann::json::array({{0, 0}, {0, 100}}));
  assert(defaults["width"] == 20 && defaults["falloff"] == 40);
  auto graph = defaultTerrainGraph();
  graph["nodes"].push_back({{"id", "path"},
                            {"type", "path_distance_mask"},
                            {"parameters", defaults}});
  assert(!rejected(graph));
  for (const auto &points :
       {nlohmann::json::array(), nlohmann::json::array({{0, 0}}),
        nlohmann::json::array({{0, 0, 0}, {1, 1}}),
        nlohmann::json::array({{0, 0}, {1.0e100, 1}})}) {
    graph["nodes"].back()["parameters"]["points"] = points;
    assert(rejected(graph));
  }
  graph["nodes"].back()["parameters"] = {
      {"points", {{1, 1}, {1, 1}}}, {"width", 0}, {"falloff", 0}};
  assert(!rejected(graph));
}

void polygonMetadataAndParserShareTheCoordinateContract() {
  const auto *definition = terrainGraphNodeDefinition("polygon_mask");
  assert(definition);
  assert(definition->inputs.size() == 1 && definition->inputs[0].required);
  assert(definition->outputs.size() == 1);
  assert(definition->outputs[0].kind == TerrainGraphValueKind::Mask);
  nlohmann::json defaults = nlohmann::json::object();
  for (const auto &parameter : definition->parameters) {
    defaults[parameter.name] = parameter.defaultValue;
  }
  assert(defaults["falloff"] == 20);
  auto graph = defaultTerrainGraph();
  graph["nodes"].push_back(
      {{"id", "polygon"}, {"type", "polygon_mask"}, {"parameters", defaults}});
  assert(!rejected(graph));
  auto omittedDefaults = graph;
  omittedDefaults["nodes"].back()["parameters"] = nlohmann::json::object();
  assert(TerrainGraph::parse(omittedDefaults).contentKey() ==
         TerrainGraph::parse(graph).contentKey());
  const auto validPolygons = nlohmann::json::parse(R"([
    [[0,0],[2,0],[2,2],[0,2]],
    [[0,0],[0,2],[2,2],[2,0],[0,0]],
    [[0,0],[0,0],[2,0],[1,1],[2,2],[0,2]],
    [[0,0],[2,2],[0,2],[2,0]]
  ])");
  for (const auto &points : validPolygons) {
    graph["nodes"].back()["parameters"]["points"] = points;
    const auto parsed = TerrainGraph::parse(graph);
    assert(parsed.node("polygon"));
    assert(parsed.node("polygon")->parameters ==
           graph["nodes"].back()["parameters"]);
    assert(TerrainGraph::parse(parsed.toJson()).contentKey() ==
           parsed.contentKey());
  }
  const auto invalidPolygons = nlohmann::json::parse(R"([
    [],
    [[0,0],[1,1]],
    [[0,0],[1,1],[2,2]],
    [[0,0],[0,0],[0,0]],
    [[0,0],[2,0,1],[0,2]],
    [[0,0],[2,0],[0,1e100]]
  ])");
  for (const auto &points : invalidPolygons) {
    graph["nodes"].back()["parameters"]["points"] = points;
    assert(rejected(graph));
  }
  graph["nodes"].back()["parameters"] = defaults;
  graph["nodes"].back()["parameters"]["falloff"] = -1;
  assert(rejected(graph));
}

TerrainRecipe erosionQualityRecipe() {
  auto recipe = smallRecipe();
  recipe.graph = noiseGraph({{"height_variation", 20}, {"feature_size", 8}});
  recipe.graph["nodes"].push_back({
      {"id", "erosion"},
      {"type", "erosion"},
      {"parameters", {{"iterations", 8}, {"thermal_strength", 0.35}}},
  });
  recipe.graph["links"][0]["from"]["node"] = "erosion";
  recipe.graph["links"].push_back({
      {"id", "source_erosion"},
      {"from", {{"node", "source"}, {"port", "field"}}},
      {"to", {{"node", "erosion"}, {"port", "field"}}},
  });
  return recipe;
}

void generationQualitySeparatesErosionCachesAndPreservesAuthoredStrokes() {
  auto recipe = erosionQualityRecipe();
  const auto preview =
      TerrainGenerator::generate(recipe, {}, {}, TerrainQuality::Preview);
  assert(preview && preview->quality == "preview");
  TerrainGraphInputs retained;
  retained.previousCache = preview->graphArtifacts->cache;
  const auto final = executeTerrainGraph(recipe, retained);
  assert(final && final->quality == "standard");
  const auto balance = final->graphArtifacts->sedimentBalance.at("erosion");
  assert(balance && balance->size() == final->heights.size());
  assert(final->cellsX == preview->cellsX && final->size.x == preview->size.x);
  assert(final->heights != preview->heights);
  const auto cached = [&](std::string_view id) {
    const auto run = std::ranges::find(final->graphArtifacts->nodes, id,
                                       &TerrainGraphNodeRun::id);
    assert(run != final->graphArtifacts->nodes.end());
    return run->cached;
  };
  assert(cached("source"));
  assert(!cached("erosion"));
  assert(!cached("terrain_output"));
  retained.previousCache = final->graphArtifacts->cache;
  TerrainEdit edit;
  edit.kind = TerrainEditKind::Raise;
  edit.center = {8, 8};
  edit.radius = 2;
  edit.strength = 0.5F;
  recipe.edits.push_back(edit);
  const auto sculpted = executeTerrainGraph(recipe, retained);
  assert(sculpted);
  assert(sculpted->baseHeights == final->heights);
  assert(sculpted->graphArtifacts->sedimentBalance.at("erosion") == balance);
  assert(sculpted->height(8, 8) > final->height(8, 8));
  for (const auto &run : sculpted->graphArtifacts->nodes) {
    assert(run.cached);
  }
}

void erosionMasksDriveDownstreamNodesAndIgnoreChunkPartition() {
  const auto recipe = erosionQualityRecipe();
  const auto baseline = executeTerrainGraph(recipe);
  assert(baseline);
  auto partitioned = recipe;
  partitioned.chunkCells = 4;
  const auto repartitioned = executeTerrainGraph(partitioned);
  assert(repartitioned && repartitioned->heights == baseline->heights);

  const auto &balance =
      *baseline->graphArtifacts->sedimentBalance.at("erosion");
  for (const std::string port : {"deposition", "cut"}) {
    auto masked = recipe;
    masked.graph["nodes"].push_back({
        {"id", "mask_offset"},
        {"type", "offset"},
        {"parameters", {{"amount", 3}}},
    });
    masked.graph["links"][0]["from"]["node"] = "mask_offset";
    masked.graph["links"].push_back({
        {"id", "erosion_offset_field"},
        {"from", {{"node", "erosion"}, {"port", "field"}}},
        {"to", {{"node", "mask_offset"}, {"port", "field"}}},
    });
    masked.graph["links"].push_back({
        {"id", "erosion_offset_mask"},
        {"from", {{"node", "erosion"}, {"port", port}}},
        {"to", {{"node", "mask_offset"}, {"port", "mask"}}},
    });
    const auto result = executeTerrainGraph(masked);
    assert(result);
    const float sign = port == "deposition" ? 1.0F : -1.0F;
    float maximum = 0;
    for (std::size_t sample = 0; sample < balance.size(); ++sample) {
      maximum = std::max(maximum, sign * balance[sample]);
    }
    for (std::size_t sample = 0; sample < balance.size(); ++sample) {
      const float weight =
          maximum > 0 ? std::max(0.0F, sign * balance[sample]) / maximum : 0.0F;
      const float expected = baseline->heights[sample] + 3.0F * weight;
      assert(std::abs(result->heights[sample] - expected) < 0.00001F);
    }
  }
}

void drainageFlowMaskDrivesGraphModifiers() {
  auto recipe = smallRecipe();
  recipe.graph = noiseGraph({{"height_variation", 20}, {"feature_size", 8}});
  const auto baseline = executeTerrainGraph(recipe);
  assert(baseline);
  const auto drainage = computeTerrainDrainage(*baseline, recipe, {});
  assert(drainage);
  recipe.graph["nodes"].push_back({{"id", "drainage"}, {"type", "drainage"}});
  recipe.graph["nodes"].push_back({
      {"id", "flow_offset"},
      {"type", "offset"},
      {"parameters", {{"amount", 2}}},
  });
  recipe.graph["links"][0]["from"]["node"] = "flow_offset";
  recipe.graph["links"].push_back({
      {"id", "source_drainage"},
      {"from", {{"node", "source"}, {"port", "field"}}},
      {"to", {{"node", "drainage"}, {"port", "field"}}},
  });
  recipe.graph["links"].push_back({
      {"id", "source_offset"},
      {"from", {{"node", "source"}, {"port", "field"}}},
      {"to", {{"node", "flow_offset"}, {"port", "field"}}},
  });
  recipe.graph["links"].push_back({
      {"id", "flow_mask"},
      {"from", {{"node", "drainage"}, {"port", "flow"}}},
      {"to", {{"node", "flow_offset"}, {"port", "mask"}}},
  });
  const auto result = executeTerrainGraph(recipe);
  assert(result);
  for (std::size_t sample = 0; sample < baseline->heights.size(); ++sample) {
    const float expected =
        baseline->heights[sample] + 2 * drainage->flow[sample];
    assert(std::abs(result->heights[sample] - expected) < 0.00001F);
  }
}

void erosionReportsIntermediateProgressAndCancelsWithoutPublication() {
  const auto recipe = erosionQualityRecipe();
  std::stop_source cancellation;
  float previousProgress = 0;
  bool interruptedErosion = false;
  const auto cancelled = executeTerrainGraph(
      recipe, {}, cancellation.get_token(), [&](float progress) {
        assert(progress >= previousProgress && progress <= 1);
        previousProgress = progress;
        if (progress > 1.0F / 3 && progress < 2.0F / 3) {
          interruptedErosion = true;
          cancellation.request_stop();
        }
      });
  assert(!cancelled && interruptedErosion);
  const auto first = TerrainGenerator::generate(recipe);
  const auto second = TerrainGenerator::generate(recipe);
  assert(first && second && first->heights == second->heights);
  const auto preview = std::make_shared<const HeightField>(
      *TerrainGenerator::generate(recipe, {}, {}, TerrainQuality::Preview));
  bool rejectedPreview = false;
  try {
    publishTerrain(recipe.toJson(), {}, preview);
  } catch (const std::invalid_argument &) {
    rejectedPreview = true;
  }
  assert(rejectedPreview);
  rejectedPreview = false;
  try {
    (void)updateTerrain(recipe, recipe, preview);
  } catch (const std::invalid_argument &) {
    rejectedPreview = true;
  }
  assert(rejectedPreview);
  TerrainCookInput cook;
  cook.field = preview.get();
  cook.recipeDigest = terrainCookRecipeDigest(recipe);
  std::string error;
  assert(!cookTerrainField(cook, &error));
  assert(error.find("Final") != std::string::npos);
}

} // namespace

int main() {
  generationQualitySeparatesErosionCachesAndPreservesAuthoredStrokes();
  erosionMasksDriveDownstreamNodesAndIgnoreChunkPartition();
  drainageFlowMaskDrivesGraphModifiers();
  erosionReportsIntermediateProgressAndCancelsWithoutPublication();
  metadataUsesCanonicalNativeDefaults();
  graphNoiseUsesTheSharedLandformEvaluator();
  noiseParameterDomainsAreValidatedBeforeExecution();
  landformControlsInvalidateTheGraphSourceCache();
  pathMetadataAndParserShareTheCoordinateContract();
  polygonMetadataAndParserShareTheCoordinateContract();
  std::cout << "Terrain graph landform controls checks passed\n";
}
