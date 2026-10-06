#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainWaterDiagnostics.h"

#include "demi/assets/AssetHash.h"
#include "demi/runtime/terrain/TerrainEvaluation.h"
#include "demi/runtime/terrain/TerrainGraphNodeEvaluation.h"
#include "demi/runtime/terrain/TerrainScatter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <span>
#include <stdexcept>
#include <utility>

namespace demi::runtime {
using terrain_graph_detail::evaluateTerrainGraphNode;
using terrain_graph_detail::GraphField;
using terrain_graph_detail::GraphInstances;
using terrain_graph_detail::GraphNodeResult;
using terrain_graph_detail::GraphValues;
using terrain_graph_detail::GraphWater;
using terrain_graph_detail::terrainGraphNodeContext;
using terrain_graph_detail::terrainGraphNodeParameters;

namespace {

struct CacheEntry {
  std::string key;
  std::string signature;
  GraphNodeResult result;
};

void refreshOutputMetadata(HeightField &field, const TerrainRecipe &recipe) {
  field.biomeIds.clear();
  field.biomeColors.clear();
  field.biomeMaterials.clear();
  field.biomeTextureScales.clear();
  for (const auto &[id, biome] : recipe.biomes) {
    field.biomeIds.push_back(id);
    field.biomeColors.push_back(biome.color);
    field.biomeMaterials.push_back(biome.material);
    field.biomeTextureScales.push_back(biome.textureScale);
  }
  field.chunks.clear();
  for (int z = 0; z < field.cellsZ;) {
    const int depth = std::min(recipe.chunkCells, field.cellsZ - z);
    for (int x = 0; x < field.cellsX;) {
      const int width = std::min(recipe.chunkCells, field.cellsX - x);
      field.chunks.push_back({x, z, width, depth});
      x += width;
    }
    z += depth;
  }
}
} // namespace

class TerrainGraphEvaluationCache {
public:
  std::map<std::string, CacheEntry> nodes;
};

TerrainGraphBiomeOverlay::TerrainGraphBiomeOverlay(const HeightField &base,
                                                   const TerrainRecipe &recipe)
    : base_(base) {
  if (base.biomeIndices.size() != recipe.sampleCount() ||
      base.biomeIds.size() != recipe.biomes.size())
    throw std::invalid_argument("Graph biome checkpoint does not match recipe");
  std::map<std::string, std::size_t> biomeIndices;
  for (const auto &[id, biome] : recipe.biomes) {
    const auto index = biomeIndices.size();
    if (base.biomeIds[index] != id)
      throw std::invalid_argument("Graph biome checkpoint palette has changed");
    biomeIndices.emplace(id, index);
  }
  TerrainEvaluation evaluation(recipe);
  regions_ = evaluation.regions();
  for (const auto *region : regions_)
    regionBiomes_.push_back(biomeIndices.at(region->biome));
}

std::size_t TerrainGraphBiomeOverlay::biomeAt(int x, int z) const {
  const auto index = base_.index(x, z);
  if (regions_.empty())
    return base_.biomeIndices.at(index);
  std::array<double, 16> localWeights{};
  std::vector<double> extendedWeights;
  std::span<double> weights;
  if (base_.biomeIds.size() <= localWeights.size())
    weights = std::span(localWeights).first(base_.biomeIds.size());
  else {
    extendedWeights.resize(base_.biomeIds.size());
    weights = extendedWeights;
  }
  weights[base_.biomeIndices.at(index)] = 1.0;
  const auto position = base_.position(x, z);
  for (std::size_t region = 0; region < regions_.size(); ++region) {
    const auto *stroke = regions_[region];
    const double alpha =
        terrainBrushWeight(position, stroke->center, stroke->radius,
                           stroke->strength, stroke->falloff);
    for (auto &weight : weights)
      weight *= 1.0 - alpha;
    weights[regionBiomes_[region]] += alpha;
  }
  return std::size_t(std::max_element(weights.begin(), weights.end()) -
                     weights.begin());
}

std::optional<HeightField>
executeTerrainGraph(const TerrainRecipe &recipe,
                    const TerrainGraphInputs &inputs, std::stop_token stop,
                    const TerrainGenerator::Progress &progress) {
  const auto graph = TerrainGraph::parse(recipe.graph);
  const auto order = graph.executionOrder();
  auto cache = std::make_shared<TerrainGraphEvaluationCache>();
  auto artifacts = std::make_shared<TerrainGraphArtifacts>();
  auto sourceRecipe = recipe;
  sourceRecipe.graph = nullptr;
  sourceRecipe.edits.clear();
  sourceRecipe.exclusions.clear();
  sourceRecipe.regions.clear();
  sourceRecipe.rules.clear();

  std::size_t completed = 0;
  for (const auto &nodeId : order) {
    if (stop.stop_requested())
      return std::nullopt;
    const auto &node = *graph.node(nodeId);
    const auto values = terrainGraphNodeParameters(node);
    nlohmann::json signature{
        {"type", node.type},
        {"parameters", values},
        {"context", terrainGraphNodeContext(sourceRecipe, node, inputs)}};
    if (node.type == "biomes")
      signature["context"] = terrainGraphNodeContext(recipe, node, inputs);

    GraphValues connected;
    for (const auto &link : graph.links) {
      if (link.to.node != nodeId)
        continue;
      const auto &upstream = cache->nodes.at(link.from.node);
      signature["inputs"][link.to.port] = {{"node", link.from.node},
                                           {"port", link.from.port},
                                           {"key", upstream.key}};
      connected.emplace(link.to.port,
                        upstream.result.outputs.at(link.from.port));
    }
    const auto serializedSignature = signature.dump();
    const auto key = assets::hashBytes(std::span(
        reinterpret_cast<const unsigned char *>(serializedSignature.data()),
        serializedSignature.size()));
    const auto begin = std::chrono::steady_clock::now();

    if (inputs.previousCache) {
      const auto old = inputs.previousCache->nodes.find(nodeId);
      if (old != inputs.previousCache->nodes.end() && old->second.key == key &&
          old->second.signature == serializedSignature) {
        cache->nodes.emplace(nodeId, old->second);
        artifacts->nodes.push_back({nodeId, true, 0});
        artifacts->warnings.insert(artifacts->warnings.end(),
                                   old->second.result.warnings.begin(),
                                   old->second.result.warnings.end());
        if (progress)
          progress(float(++completed) / order.size());
        continue;
      }
    }

    std::optional<GraphNodeResult> evaluated;
    try {
      evaluated = evaluateTerrainGraphNode(node, values, sourceRecipe, recipe,
                                           inputs, connected, stop);
    } catch (const std::exception &error) {
      throw std::invalid_argument("Node " + nodeId + ": " + error.what());
    }
    if (!evaluated)
      return std::nullopt;
    artifacts->warnings.insert(artifacts->warnings.end(),
                               evaluated->warnings.begin(),
                               evaluated->warnings.end());
    cache->nodes.emplace(
        nodeId, CacheEntry{key, serializedSignature, std::move(*evaluated)});
    artifacts->nodes.push_back({nodeId, false,
                                std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - begin)
                                    .count()});
    if (progress)
      progress(float(++completed) / order.size());
  }

  const auto &outputs = cache->nodes.at(graph.output).result.outputs;
  artifacts->baseField = std::get<GraphField>(outputs.at("field"));
  auto result = *artifacts->baseField;
  result.baseHeights = result.heights;
  refreshOutputMetadata(result, recipe);
  if (stop.stop_requested())
    return std::nullopt;
  TerrainGraphBiomeOverlay overlay(*artifacts->baseField, recipe);
  if (stop.stop_requested())
    return std::nullopt;
  for (int z = 0; z <= result.cellsZ; ++z) {
    if (stop.stop_requested())
      return std::nullopt;
    for (int x = 0; x <= result.cellsX; ++x) {
      if ((x & 255) == 0 && stop.stop_requested())
        return std::nullopt;
      const auto index = result.index(x, z);
      result.biomeIndices.set(index, overlay.biomeAt(x, z));
    }
  }
  if (!TerrainGenerator::applyTerrainSurfaceLayers(result, recipe, stop))
    return std::nullopt;

  if (const auto found = outputs.find("water"); found != outputs.end()) {
    if (stop.stop_requested())
      return std::nullopt;
    const auto water =
        std::get<std::shared_ptr<const GraphWater>>(found->second);
    artifacts->water = water->authoring;
    auto refreshed = refreshTerrainWaterResult(result, artifacts->water,
                                               result.heights, stop);
    if (stop.stop_requested())
      return std::nullopt;
    if (!refreshed || refreshed->dropped)
      throw std::invalid_argument(
          "Graph water does not match the final terrain surface");
    artifacts->waterResult = std::move(*refreshed);
    const auto warnings =
        terrainWaterContainmentWarnings(result, artifacts->water);
    artifacts->warnings.insert(artifacts->warnings.end(), warnings.begin(),
                               warnings.end());
  }
  result.scatterPlacements.clear();
  result.scatterTruncated = false;
  if (const auto found = outputs.find("instances"); found != outputs.end()) {
    const auto instances =
        std::get<std::shared_ptr<const GraphInstances>>(found->second);
    artifacts->basePlacements =
        std::shared_ptr<const std::vector<TerrainScatterPlacement>>(
            instances, &instances->placements);
    result.scatterTruncated = instances->truncated;
    if (instances->truncated)
      artifacts->warnings.push_back(
          "Scatter placement budget reached; some placements were omitted.");
  }
  const auto candidates =
      artifacts->basePlacements
          ? std::span<const TerrainScatterPlacement>(*artifacts->basePlacements)
          : std::span<const TerrainScatterPlacement>{};
  if (!refreshTerrainScatterPlacements(
          result, candidates,
          artifacts->water.bodies.empty() ? nullptr : &artifacts->water, stop))
    return std::nullopt;
  if (inputs.palette) {
    result.paletteId = inputs.palette->id;
    result.resolvedPalette =
        std::make_shared<const TerrainPalette>(*inputs.palette);
  }
  result.inputFingerprint = inputs.inputFingerprint;
  result.stageOrder = "terrain_graph,surface_layers";
  if (stop.stop_requested())
    return std::nullopt;
  artifacts->cache = std::move(cache);
  result.graphArtifacts = std::move(artifacts);
  if (stop.stop_requested())
    return std::nullopt;
  return result;
}
} // namespace demi::runtime
