#include "demi/runtime/terrain/TerrainGraphNodeEvaluation.h"
#include "demi/runtime/terrain/TerrainGraphSedimentMasks.h"
#include "demi/runtime/terrain/TerrainGraphSpatialMasks.h"
#include "demi/runtime/terrain/TerrainGraphPointMath.h"
#include "demi/runtime/terrain/TerrainGraphWater.h"
#include "demi/runtime/terrain/TerrainGraphWaterAuthoring.h"
#include "demi/runtime/terrain/TerrainSurfaceSediment.h"

#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainErosion.h"
#include "demi/runtime/terrain/TerrainScatter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace demi::runtime::terrain_graph_detail {
namespace {

const GraphValue *input(const GraphValues &connected, std::string_view port) {
  const auto found = connected.find(std::string(port));
  return found == connected.end() ? nullptr : &found->second;
}

GraphField fieldInput(const GraphValues &connected, std::string_view port) {
  const auto *value = input(connected, port);
  if (!value)
    throw std::invalid_argument("Field input is missing: " + std::string(port));
  return std::get<GraphField>(*value);
}

void requireSameGrid(const HeightField &a, const HeightField &b) {
  if (a.cellsX != b.cellsX || a.cellsZ != b.cellsZ || a.size.x != b.size.x ||
      a.size.y != b.size.y)
    throw std::invalid_argument(
        "Connected terrain data must use the same sample grid");
}

std::optional<GraphNodeResult> evaluateSource(const TerrainGraphNode &node,
                                              const nlohmann::json &values,
                                              const TerrainRecipe &sourceRecipe,
                                              std::stop_token stop) {
  auto settings = sourceRecipe;
  if (node.type != "landform") {
    TerrainLandform shape;
    if (node.type == "constant") {
      shape.baseHeight = values.at("height").get<float>();
      shape.heightVariation = 0;
    } else {
      auto landformParameters = values;
      landformParameters.erase("seed_offset");
      shape = TerrainLandform::parse(landformParameters);
      const auto seed =
          std::int64_t(settings.seed) + values.at("seed_offset").get<int>();
      if (seed < INT32_MIN || seed > INT32_MAX) {
        throw std::invalid_argument("Seed offset overflows the seed range");
      }
      settings.seed = static_cast<int>(seed);
    }
    settings.landforms = {{"graph", shape}};
    settings.defaultLandform = "graph";
    for (auto &[id, biome] : settings.biomes) {
      biome.landform = "graph";
    }
    settings.rules.clear();
    settings.regions.clear();
  }
  auto generated = TerrainGenerator::generateBase(settings, stop);
  if (!generated ||
      !TerrainGenerator::recomputeTerrainNormals(*generated, stop)) {
    return std::nullopt;
  }
  GraphNodeResult result;
  result.outputs["field"] =
      std::make_shared<const HeightField>(std::move(*generated));
  return result;
}

std::optional<GraphNodeResult> evaluateMask(const TerrainGraphNode &node,
                                            const nlohmann::json &values,
                                            const GraphValues &connected,
                                            std::stop_token stop) {
  auto mask = std::make_shared<GraphMask>();
  mask->source = fieldInput(connected, "field");
  mask->samples.resize(mask->source->heights.size());
  if (stop.stop_requested())
    return std::nullopt;
  for (std::size_t index = 0; index < mask->samples.size(); ++index) {
    if ((index & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    const auto value =
        node.type == "elevation_mask"
            ? mask->source->heights[index]
            : float(std::acos(std::clamp(mask->source->normals[index].y, -1.0F,
                                         1.0F)) *
                    180 / 3.141592653589793);
    mask->samples.set(index, terrainGraphBandWeight(value, values.at("minimum"),
                                  values.at("maximum"), values.at("falloff")));
  }
  GraphNodeResult result;
  result.outputs["mask"] = std::shared_ptr<const GraphMask>(std::move(mask));
  return result;
}

std::optional<GraphNodeResult>
evaluateHeightModifier(const TerrainGraphNode &node,
                       const nlohmann::json &values,
                       const GraphValues &connected, std::stop_token stop) {
  const auto a =
      fieldInput(connected, node.type == "height_blend" ? "a" : "field");
  auto field = std::make_shared<HeightField>(*a);
  GraphField b;
  if (node.type == "height_blend") {
    b = fieldInput(connected, "b");
    requireSameGrid(*a, *b);
  }
  std::shared_ptr<const GraphMask> mask;
  if (const auto *value = input(connected, "mask")) {
    mask = std::get<std::shared_ptr<const GraphMask>>(*value);
    requireSameGrid(*a, *mask->source);
  }
  const double amount = values.at("amount").get<double>();
  if (b && (amount < 0 || amount > 1))
    throw std::invalid_argument("Blend amount must be in [0,1]");
  for (std::size_t index = 0; index < a->heights.size(); ++index) {
    if ((index & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    field->heights.set(index, terrainGraphModifiedHeight(
        a->heights[index], b ? b->heights[index] : 0, amount,
        mask ? mask->samples[index] : 1, b != nullptr));
  }
  if (b) {
    const auto sediment = blendTerrainSurfaceSediment(
        *a, *b, mask ? &mask->samples : nullptr, amount, stop);
    if (!sediment) {
      return std::nullopt;
    }
    field->surfaceSedimentBalance = *sediment;
  }
  if (!TerrainGenerator::recomputeTerrainNormals(*field, stop))
    return std::nullopt;
  GraphNodeResult result;
  result.outputs["field"] = GraphField(std::move(field));
  return result;
}

std::optional<GraphNodeResult>
evaluateDrainage(const nlohmann::json &values,
                 const TerrainRecipe &sourceRecipe,
                 const GraphValues &connected, std::stop_token stop) {
  auto drainage = std::make_shared<GraphDrainage>();
  drainage->source = fieldInput(connected, "field");
  TerrainDrainageSettings settings;
  settings.seaLevel = values.at("sea_level").get<float>();
  auto computed =
      computeTerrainDrainage(*drainage->source, sourceRecipe, settings, stop);
  if (stop.stop_requested() || !computed)
    return std::nullopt;
  drainage->data = std::move(*computed);
  auto flow = std::make_shared<GraphMask>();
  flow->source = drainage->source;
  flow->samples = drainage->data.flow;
  GraphNodeResult result;
  result.outputs["flow"] = std::shared_ptr<const GraphMask>(std::move(flow));
  result.outputs["drainage"] =
      std::shared_ptr<const GraphDrainage>(std::move(drainage));
  return result;
}

std::optional<GraphNodeResult>
evaluateErosion(const nlohmann::json &values, const TerrainRecipe &sourceRecipe,
                const TerrainRecipe &authoredRecipe,
                const GraphValues &connected, std::stop_token stop,
                TerrainQuality quality,
                const TerrainGenerator::Progress &progress) {
  const auto source = fieldInput(connected, "field");
  TerrainDrainage drainage;
  if (const auto *value = input(connected, "drainage")) {
    const auto supplied =
        std::get<std::shared_ptr<const GraphDrainage>>(*value);
    requireSameGrid(*source, *supplied->source);
    if (source->heights != supplied->source->heights)
      throw std::invalid_argument("Drainage belongs to a different surface");
    drainage = supplied->data;
  } else {
    auto computed = computeTerrainDrainage(*source, sourceRecipe, {}, stop);
    if (stop.stop_requested() || !computed)
      return std::nullopt;
    drainage = std::move(*computed);
  }
  TerrainErosionSettings settings;
  settings.iterations = values.at("iterations").get<int>();
  settings.thermalStrength = values.at("thermal_strength").get<float>();
  settings.seed = authoredRecipe.seed;
  settings.quality = quality;
  TerrainSamples<float> balance;
  TerrainErosionTransport transport;
  auto eroded = applyTerrainErosion(*source, drainage, settings, &balance, stop,
                                    progress, &transport);
  if (stop.stop_requested() || !eroded)
    return std::nullopt;
  auto field = std::make_shared<HeightField>(*source);
  field->heights = std::move(*eroded);
  const auto selectedSediment =
      accumulateTerrainSurfaceSediment(*source, balance, stop);
  if (!selectedSediment) {
    return std::nullopt;
  }
  field->surfaceSedimentBalance = *selectedSediment;
  if (!TerrainGenerator::recomputeTerrainNormals(*field, stop))
    return std::nullopt;
  auto sediment =
      prepareTerrainGraphSedimentMasks(field, std::move(balance), stop);
  if (!sediment) {
    return std::nullopt;
  }
  GraphNodeResult result;
  result.outputs["field"] = GraphField(std::move(field));
  result.outputs["deposition"] = std::move(sediment->deposition);
  result.outputs["cut"] = std::move(sediment->cut);
  result.sedimentBalance = std::move(sediment->balance);
  result.hydraulicTransport = transport;
  std::ostringstream status;
  status << std::setprecision(6) << "Hydraulic sample-height totals: eroded "
         << transport.erodedSediment << ", deposited "
         << transport.depositedSediment << ", exported "
         << transport.exportedSediment << ", suspended "
         << transport.suspendedSediment
         << ". Suspended is unfinished transport, not retained fluid state.";
  result.status = status.str();
  return result;
}

std::optional<std::vector<std::uint8_t>>
waterWetSamplesInput(const GraphValues &connected, const HeightField &field,
                     std::stop_token stop) {
  const auto *value = input(connected, "water");
  if (value == nullptr) {
    return std::nullopt;
  }
  const auto &water = *std::get<std::shared_ptr<const GraphWater>>(*value);
  auto coverage = water.data.resolvedCoverage;
  if (coverage != nullptr) {
    const bool matchingGrid =
        coverage->cellsX == field.cellsX && coverage->cellsZ == field.cellsZ &&
        coverage->size.x == field.size.x && coverage->size.y == field.size.y;
    if (!matchingGrid) {
      throw std::invalid_argument(
          "Connected water data does not match the input field grid");
    }
  } else if (!water.authoring.authored || !water.authoring.bodies.empty()) {
    throw std::invalid_argument(
        "Connected water data has no resolved coverage");
  }
  if (coverage == nullptr || water.data.carvedHeights != field.heights) {
    // Refresh coverage after downstream height operations without recarving or
    // preparing render meshes. An empty route is authoritative all-dry water.
    auto refreshed = terrain_water_detail::buildWaterLevelField(
        field.size, field.cellsX, field.cellsZ, field.heights, water.authoring,
        stop);
    if (stop.stop_requested()) {
      return std::nullopt;
    }
    coverage = std::make_shared<const terrain_water_detail::WaterLevelField>(
        std::move(refreshed));
  }
  return terrain_water_detail::submergedWaterSamples(field, *coverage, stop);
}

std::optional<GraphNodeResult>
evaluateBiomes(const TerrainRecipe &sourceRecipe,
               const TerrainRecipe &authoredRecipe,
               const GraphValues &connected, std::stop_token stop) {
  auto field = std::make_shared<HeightField>(*fieldInput(connected, "field"));
  auto biomeRecipe = sourceRecipe;
  biomeRecipe.rules = authoredRecipe.rules;
  std::vector<float> heights(field->heights.size());
  for (std::size_t index = 0; index < heights.size(); ++index) {
    if ((index & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    heights[index] = field->heights[index];
  }
  if (stop.stop_requested())
    return std::nullopt;
  const auto wetSamples = waterWetSamplesInput(connected, *field, stop);
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  TerrainRuleContextBuilder contexts(biomeRecipe);
  contexts.build(biomeRecipe, heights, field->cellsX, field->cellsZ,
                 field->size, wetSamples ? &*wetSamples : nullptr);
  if (stop.stop_requested())
    return std::nullopt;
  const auto decisions =
      assignTerrainBiomes(biomeRecipe, contexts.contexts(), field->cellsX,
                          field->cellsZ, field->size);
  if (stop.stop_requested())
    return std::nullopt;
  for (std::size_t index = 0; index < decisions.size(); ++index) {
    if ((index & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    field->biomeIndices.set(index, decisions[index].biome);
  }
  GraphNodeResult result;
  result.outputs["field"] = GraphField(std::move(field));
  return result;
}

std::optional<GraphNodeResult>
evaluateScatter(const TerrainRecipe &sourceRecipe,
                const TerrainGraphInputs &inputs, const GraphValues &connected,
                std::stop_token stop) {
  GraphNodeResult result;
  auto placements = std::make_shared<GraphInstances>();
  if (inputs.palette) {
    auto field = *fieldInput(connected, "field");
    if (field.exclusions.empty())
      field.exclusions.resize(field.heights.size());
    const auto coverage = waterWetSamplesInput(connected, field, stop);
    if (stop.stop_requested()) {
      return std::nullopt;
    }
    const auto wetSamples = coverage ? std::span<const std::uint8_t>(*coverage)
                                     : std::span<const std::uint8_t>{};
    auto scattered = scatterTerrain(field, sourceRecipe, *inputs.palette, {},
                                    nullptr, wetSamples, stop);
    if (stop.stop_requested())
      return std::nullopt;
    placements->placements = std::move(scattered.placements);
    placements->truncated = scattered.truncated;
  } else {
    result.warnings.push_back("Scatter Palette has no resolved asset palette.");
  }
  result.outputs["instances"] =
      std::shared_ptr<const GraphInstances>(std::move(placements));
  return result;
}

GraphNodeResult evaluateOutput(const GraphValues &connected) {
  GraphNodeResult result;
  result.outputs["field"] = fieldInput(connected, "field");
  for (const char *port : {"water", "instances"})
    if (const auto *value = input(connected, port))
      result.outputs[port] = *value;
  return result;
}

} // namespace

nlohmann::json terrainGraphNodeParameters(const TerrainGraphNode &node) {
  auto result = node.parameters;
  for (const auto &field : terrainGraphNodeDefinition(node.type)->parameters)
    if (!result.contains(field.name))
      result[field.name] = field.defaultValue;
  return result;
}

nlohmann::json terrainGraphNodeContext(const TerrainRecipe &recipe,
                                       const TerrainGraphNode &node,
                                       const TerrainGraphInputs &inputs) {
  nlohmann::json context{{"size", {recipe.size.x, recipe.size.y}},
                         {"resolution", {recipe.cellsX, recipe.cellsZ}}};
  const bool source = node.type == "landform" || node.type == "noise" ||
                      node.type == "constant";
  if (source) {
    context["default_biome"] = recipe.defaultBiome;
    context["biomes"] = nlohmann::json::array();
    for (const auto &[id, biome] : recipe.biomes)
      context["biomes"].push_back(id);
    const auto generation = std::find_if(
        recipe.layers.begin(), recipe.layers.end(), [](const auto &layer) {
          return layer.kind == TerrainLayerKind::Generation;
        });
    context["generation_enabled"] =
        generation != recipe.layers.end() && generation->enabled;
  }
  if (node.type == "landform") {
    context["seed"] = recipe.seed;
    context["default_landform"] = recipe.defaultLandform;
    context["landforms"] = nlohmann::json::object();
    for (const auto &[id, shape] : recipe.landforms) {
      context["landforms"][id] = shape.toJson();
    }
    context["biome_landforms"] = nlohmann::json::object();
    for (const auto &[id, biome] : recipe.biomes)
      context["biome_landforms"][id] = biome.landform;
  } else if (node.type == "noise" || node.type == "erosion") {
    context["seed"] = recipe.seed;
  } else if (node.type == "biomes") {
    context["seed"] = recipe.seed;
    context["sea_level"] = TerrainRuleContextBuilder::seaLevel(recipe);
    context["default_biome"] = recipe.defaultBiome;
    context["biomes"] = nlohmann::json::array();
    for (const auto &[id, biome] : recipe.biomes)
      context["biomes"].push_back(id);
    context["rules"] = nlohmann::json::array();
    for (const auto &rule : recipe.rules)
      context["rules"].push_back(rule.toJson());
    context["biome_layers"] = nlohmann::json::array();
    for (const auto &layer : recipe.layers)
      if (layer.kind == TerrainLayerKind::Biome)
        context["biome_layers"].push_back({layer.id, layer.enabled});
  } else if (node.type == "scatter") {
    context["seed"] = recipe.seed;
    context["sea_level"] = TerrainRuleContextBuilder::seaLevel(recipe);
    context["palette"] = recipe.paletteId;
    context["palette_fingerprint"] = inputs.inputFingerprint;
    context["has_palette"] = inputs.palette != nullptr;
  }
  return context;
}

std::optional<GraphNodeResult> evaluateTerrainGraphNode(
    const TerrainGraphNode &node, const nlohmann::json &values,
    const TerrainRecipe &sourceRecipe, const TerrainRecipe &authoredRecipe,
    const TerrainGraphInputs &inputs, const GraphValues &connected,
    std::stop_token stop, TerrainQuality quality,
    const TerrainGenerator::Progress &progress) {
  if (stop.stop_requested())
    return std::nullopt;
  if (node.type == "landform" || node.type == "noise" ||
      node.type == "constant")
    return evaluateSource(node, values, sourceRecipe, stop);
  if (node.type == "path_distance_mask") {
    return evaluateTerrainPathMask(values, fieldInput(connected, "field"),
                                   stop);
  }
  if (node.type == "polygon_mask") {
    return evaluateTerrainPolygonMask(values, fieldInput(connected, "field"),
                                      stop);
  }
  if (node.type == "elevation_mask" || node.type == "slope_mask")
    return evaluateMask(node, values, connected, stop);
  if (node.type == "height_blend" || node.type == "offset")
    return evaluateHeightModifier(node, values, connected, stop);
  if (node.type == "drainage")
    return evaluateDrainage(values, sourceRecipe, connected, stop);
  if (node.type == "erosion")
    return evaluateErosion(values, sourceRecipe, authoredRecipe, connected,
                           stop, quality, progress);
  if (node.type == "biomes")
    return evaluateBiomes(sourceRecipe, authoredRecipe, connected, stop);
  if (node.type == "basin_water") {
    return evaluateTerrainBasinWater(node, values, connected, stop);
  }
  if (node.type == "water_edit") {
    return evaluateTerrainWaterEdit(values, connected, stop);
  }
  if (node.type == "river_route") {
    return evaluateTerrainRiverRoute(node, values, connected, stop);
  }
  if (node.type == "water")
    return evaluateTerrainGraphWater(node, values, connected, stop);
  if (node.type == "scatter")
    return evaluateScatter(sourceRecipe, inputs, connected, stop);
  if (node.type == "output")
    return evaluateOutput(connected);
  throw std::invalid_argument("Unsupported terrain graph node type: " +
                              node.type);
}

} // namespace demi::runtime::terrain_graph_detail
