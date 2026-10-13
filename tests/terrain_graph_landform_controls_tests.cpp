#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainGraphRegistry.h"
#include "demi/runtime/terrain/TerrainLandform.h"
#include "demi/runtime/terrain/TerrainRecipe.h"

#include <algorithm>
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
  graph["nodes"].push_back({{"id", "polygon"},
                            {"type", "polygon_mask"},
                            {"parameters", defaults}});
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
    assert(TerrainGraph::parse(parsed.toJson()).contentKey() == parsed.contentKey());
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

} // namespace

int main() {
  metadataUsesCanonicalNativeDefaults();
  graphNoiseUsesTheSharedLandformEvaluator();
  noiseParameterDomainsAreValidatedBeforeExecution();
  landformControlsInvalidateTheGraphSourceCache();
  pathMetadataAndParserShareTheCoordinateContract();
  polygonMetadataAndParserShareTheCoordinateContract();
  std::cout << "Terrain graph landform controls checks passed\n";
}
