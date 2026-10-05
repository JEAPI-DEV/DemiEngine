#include "demi/runtime/terrain/TerrainGraphNodeEvaluation.h"

#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainErosion.h"
#include "demi/runtime/terrain/TerrainScatter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

float checkedHeight(double value) {
  const float converted = static_cast<float>(value);
  if (!std::isfinite(converted))
    throw std::invalid_argument("Terrain graph produced a nonfinite height");
  return converted;
}

float band(float value, float minimum, float maximum, float falloff) {
  if (minimum > maximum || falloff < 0)
    throw std::invalid_argument(
        "Mask band requires minimum <= maximum and nonnegative falloff");
  if (value >= minimum && value <= maximum)
    return 1;
  if (falloff == 0)
    return 0;
  return std::clamp(value < minimum ? (value - minimum + falloff) / falloff
                                    : (maximum + falloff - value) / falloff,
                    0.0F, 1.0F);
}

std::optional<GraphNodeResult>
evaluateSource(const TerrainGraphNode &node, const nlohmann::json &values,
               const TerrainRecipe &sourceRecipe, std::stop_token stop) {
  auto settings = sourceRecipe;
  if (node.type != "landform") {
    TerrainLandform shape;
    if (node.type == "constant") {
      shape.baseHeight = values.at("height").get<float>();
      shape.heightVariation = 0;
    } else {
      shape.baseHeight = values.at("base_height").get<float>();
      shape.heightVariation = values.at("height_variation").get<float>();
      shape.featureSize = values.at("feature_size").get<float>();
      shape.roughness = values.at("roughness").get<float>();
      shape.octaves = values.at("octaves").get<int>();
      const auto seed = std::int64_t(settings.seed) +
                        values.at("seed_offset").get<int>();
      if (seed < INT32_MIN || seed > INT32_MAX)
        throw std::invalid_argument("Seed offset overflows the seed range");
      settings.seed = static_cast<int>(seed);
    }
    settings.landforms = {{"graph", shape}};
    settings.defaultLandform = "graph";
    for (auto &[id, biome] : settings.biomes)
      biome.landform = "graph";
    settings.rules.clear();
    settings.regions.clear();
  }
  auto generated = TerrainGenerator::generateBase(settings, stop);
  if (!generated ||
      !TerrainGenerator::recomputeTerrainNormals(*generated, stop))
    return std::nullopt;
  GraphNodeResult result;
  result.outputs["field"] =
      std::make_shared<const HeightField>(std::move(*generated));
  return result;
}

std::optional<GraphNodeResult>
evaluateMask(const TerrainGraphNode &node, const nlohmann::json &values,
             const GraphValues &connected, std::stop_token stop) {
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
            : float(std::acos(std::clamp(mask->source->normals[index].y,
                                         -1.0F, 1.0F)) *
                    180 / 3.141592653589793);
    mask->samples.set(index, band(value, values.at("minimum"),
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
  const auto a = fieldInput(connected,
                            node.type == "height_blend" ? "a" : "field");
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
    const double weight = amount * (mask ? mask->samples[index] : 1);
    field->heights.set(
        index, checkedHeight(b ? a->heights[index] * (1 - weight) +
                                     b->heights[index] * weight
                               : a->heights[index] + weight));
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
  auto computed = computeTerrainDrainage(*drainage->source, sourceRecipe,
                                         settings, stop);
  if (stop.stop_requested() || !computed)
    return std::nullopt;
  drainage->data = std::move(*computed);
  GraphNodeResult result;
  result.outputs["drainage"] =
      std::shared_ptr<const GraphDrainage>(std::move(drainage));
  return result;
}

std::optional<GraphNodeResult>
evaluateErosion(const nlohmann::json &values,
                const TerrainRecipe &sourceRecipe,
                const TerrainRecipe &authoredRecipe,
                const GraphValues &connected, std::stop_token stop) {
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
  auto eroded = applyTerrainErosion(*source, drainage, settings, nullptr, stop);
  if (stop.stop_requested() || !eroded)
    return std::nullopt;
  auto field = std::make_shared<HeightField>(*source);
  field->heights = std::move(*eroded);
  if (!TerrainGenerator::recomputeTerrainNormals(*field, stop))
    return std::nullopt;
  GraphNodeResult result;
  result.outputs["field"] = GraphField(std::move(field));
  return result;
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
  TerrainRuleContextBuilder contexts(biomeRecipe);
  contexts.build(biomeRecipe, heights, field->cellsX, field->cellsZ,
                 field->size);
  if (stop.stop_requested())
    return std::nullopt;
  const auto decisions = assignTerrainBiomes(
      biomeRecipe, contexts.contexts(), field->cellsX, field->cellsZ,
      field->size);
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
evaluateWater(const TerrainGraphNode &node, const nlohmann::json &values,
              const GraphValues &connected, std::stop_token stop) {
  const auto source = fieldInput(connected, "field");
  auto water = std::make_shared<GraphWater>();
  water->authoring.authored = true;
  if (const auto *upstream = input(connected, "water")) {
    const auto previous = std::get<std::shared_ptr<const GraphWater>>(*upstream);
    if (previous->data.carvedHeights != source->heights)
      throw std::invalid_argument(
          "Connected water data does not match the input field");
    water->authoring = previous->authoring;
  }
  TerrainWaterBodySpec body;
  body.id = node.id;
  if (std::ranges::any_of(water->authoring.bodies, [&](const auto &entry) {
        return entry.id == body.id;
      }))
    throw std::invalid_argument("Duplicate water body ID: " + body.id);
  const auto kind = values.at("kind").get<std::string>();
  body.kind = kind == "river"   ? TerrainWaterBody::River
              : kind == "ocean" ? TerrainWaterBody::Ocean
                                : TerrainWaterBody::Lake;
  body.level = values.at("level").get<float>();
  body.center = {values.at("center_x").get<float>(),
                 values.at("center_z").get<float>()};
  body.radius = values.at("radius").get<float>();
  body.riverWidth = values.at("river_width").get<float>();
  for (const auto &point : values.at("river_path")) {
    if (stop.stop_requested())
      return std::nullopt;
    const auto position = point.get<std::array<float, 3>>();
    body.riverPath.push_back({position[0], position[1], position[2]});
  }
  TerrainWaterAuthoring nextBody;
  nextBody.authored = true;
  nextBody.bodies.push_back(body);
  auto carved = carveTerrainWater(*source, nextBody, nullptr, stop);
  if (stop.stop_requested())
    return std::nullopt;
  if (!carved || carved->dropped)
    throw std::invalid_argument("Water body could not be placed on this surface");
  auto field = std::make_shared<HeightField>(*source);
  field->heights = std::move(carved->carvedHeights);
  if (!TerrainGenerator::recomputeTerrainNormals(*field, stop))
    return std::nullopt;
  water->authoring.bodies.push_back(std::move(body));
  auto resolved = refreshTerrainWaterResult(
      *field, water->authoring, field->heights, stop);
  if (stop.stop_requested())
    return std::nullopt;
  if (!resolved || resolved->dropped)
    throw std::invalid_argument("Combined water bodies could not be resolved");
  water->data = std::move(*resolved);
  GraphNodeResult result;
  result.outputs["field"] = GraphField(std::move(field));
  result.outputs["water"] = std::shared_ptr<const GraphWater>(std::move(water));
  return result;
}

std::optional<GraphNodeResult>
evaluateScatter(const TerrainRecipe &sourceRecipe,
                const TerrainGraphInputs &inputs,
                const GraphValues &connected, std::stop_token stop) {
  GraphNodeResult result;
  auto placements = std::make_shared<GraphInstances>();
  if (inputs.palette) {
    auto field = *fieldInput(connected, "field");
    if (field.exclusions.empty())
      field.exclusions.resize(field.heights.size());
    auto scattered = scatterTerrain(field, sourceRecipe, *inputs.palette);
    if (stop.stop_requested())
      return std::nullopt;
    placements->placements = std::move(scattered.placements);
    placements->truncated = scattered.truncated;
  } else {
    result.warnings.push_back(
        "Scatter Palette has no resolved asset palette.");
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
    for (const auto &[id, shape] : recipe.landforms)
      context["landforms"][id] =
          {shape.baseHeight, shape.heightVariation, shape.featureSize,
           shape.roughness, shape.octaves};
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
    std::stop_token stop) {
  if (stop.stop_requested())
    return std::nullopt;
  if (node.type == "landform" || node.type == "noise" ||
      node.type == "constant")
    return evaluateSource(node, values, sourceRecipe, stop);
  if (node.type == "elevation_mask" || node.type == "slope_mask")
    return evaluateMask(node, values, connected, stop);
  if (node.type == "height_blend" || node.type == "offset")
    return evaluateHeightModifier(node, values, connected, stop);
  if (node.type == "drainage")
    return evaluateDrainage(values, sourceRecipe, connected, stop);
  if (node.type == "erosion")
    return evaluateErosion(values, sourceRecipe, authoredRecipe, connected,
                           stop);
  if (node.type == "biomes")
    return evaluateBiomes(sourceRecipe, authoredRecipe, connected, stop);
  if (node.type == "water")
    return evaluateWater(node, values, connected, stop);
  if (node.type == "scatter")
    return evaluateScatter(sourceRecipe, inputs, connected, stop);
  if (node.type == "output")
    return evaluateOutput(connected);
  throw std::invalid_argument("Unsupported terrain graph node type: " +
                              node.type);
}

} // namespace demi::runtime::terrain_graph_detail
