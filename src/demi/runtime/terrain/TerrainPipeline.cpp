#include "demi/runtime/terrain/TerrainPipeline.h"
#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainDrainage.h"
#include "demi/runtime/terrain/TerrainErosion.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainScatter.h"
#include "demi/runtime/terrain/TerrainScatterConstraints.h"
#include "demi/runtime/terrain/TerrainWater.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace demi::runtime {
namespace {

// Records the stage, so a result says what produced it rather than leaving a
// caller to infer it from the shape of the data.
void record(TerrainGenerationResult &result, const TerrainStage stage) {
  result.stages.push_back(stage);
}

// The fingerprint is what a consumer compares to decide whether two results
// describe the same terrain. It mixes the stage order and the quality tier,
// because a preview and a standard result of the same recipe are different
// terrain and must never be treated as interchangeable.
std::uint64_t fingerprintOf(const std::vector<TerrainStage> &stages,
                            const TerrainQuality quality) {
  std::uint64_t hash = 14695981039346656037ull;
  for (const auto stage : stages) {
    for (const char character : terrainStageName(stage)) {
      hash ^= static_cast<unsigned char>(character);
      hash *= 1099511628211ull;
    }
  }
  for (const char character : terrainQualityName(quality)) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ull;
  }
  return hash;
}

// The rule context supplies slope and moisture. Final drainage supplies water
// distance and flow after processing has changed the surface.
bool buildMasks(const HeightField &field, const TerrainRecipe &recipe,
                const TerrainDrainage *drainage,
                const TerrainSamples<float> *sediment, TerrainMasks &out,
                bool includeFlow, std::stop_token stop) {
  const auto count = field.heights.size();
  if (count == 0)
    return false;
  std::vector<float> heights(count);
  for (std::size_t index = 0; index < count; ++index) {
    if ((index & 255) == 0 && stop.stop_requested())
      return false;
    heights[index] = field.heights[index];
  }

  if (stop.stop_requested())
    return false;
  TerrainRuleContextBuilder contexts(recipe);
  contexts.build(recipe, heights, field.cellsX, field.cellsZ, field.size);
  if (stop.stop_requested())
    return false;
  const auto &derived = contexts.contexts();
  if (derived.size() != count)
    return false;

  out.slope.resize(count);
  out.moisture.resize(count);
  out.waterDistance.resize(count);
  out.flow.resize(count);
  out.sediment.resize(count);
  out.substrate.resize(count);
  const float shoreBand =
      std::max(field.size.x / field.cellsX, field.size.y / field.cellsZ) * 2.F;
  const bool useAuthoredSeaLevel =
      drainage != nullptr &&
      drainage->seaLevel != TerrainRuleContextBuilder::seaLevel(recipe);
  for (std::size_t index = 0; index < count; ++index) {
    if ((index & 255) == 0 && stop.stop_requested())
      return false;
    out.slope.set(index, derived[index].slope);
    out.moisture.set(index, derived[index].moisture);
    const float waterDistance = drainage != nullptr
                                    ? drainage->waterDistance[index]
                                    : derived[index].waterDistance;
    out.waterDistance.set(index, waterDistance);
    // Rule contexts use the recipe's inferred sea level. When the caller has
    // authored another level, classify the final substrate against that level.
    auto substrate = derived[index].substrate;
    if (useAuthoredSeaLevel) {
      if (heights[index] <= drainage->seaLevel)
        substrate = TerrainSubstrate::Wet;
      else if (derived[index].slope >= 38.F)
        substrate = TerrainSubstrate::Rock;
      else if (waterDistance <= shoreBand)
        substrate = TerrainSubstrate::Sand;
      else
        substrate = TerrainSubstrate::Soil;
    }
    out.substrate.set(index, static_cast<std::size_t>(substrate));
    out.flow.set(index, includeFlow && drainage != nullptr &&
                                index < drainage->flow.size()
                             ? drainage->flow[index]
                             : 0.F);
    out.sediment.set(index, sediment != nullptr ? (*sediment)[index] : 0.F);
  }
  return true;
}

void recordGraphStages(TerrainGenerationResult &result,
                       const TerrainGraph &graph,
                       const TerrainGraphArtifacts &artifacts) {
  for (const auto &run : artifacts.nodes) {
    const auto &type = graph.node(run.id)->type;
    std::optional<TerrainStage> stage;
    if (type == "landform" || type == "noise" || type == "constant")
      stage = TerrainStage::Landform;
    else if (type == "drainage")
      stage = TerrainStage::Drainage;
    else if (type == "erosion")
      stage = TerrainStage::Erosion;
    else if (type == "water")
      stage = TerrainStage::Hydrology;
    else if (type == "biomes")
      stage = TerrainStage::Surface;
    if (stage && !terrainStageRan(result, *stage))
      record(result, *stage);
  }
  record(result, TerrainStage::Sculpt);
}

std::optional<TerrainGenerationResult> generateGraphStages(
    const TerrainRecipe &recipe, const TerrainPipelineSettings &settings,
    std::stop_token stop, const TerrainProgress &progress) {
  if (settings.erosion ||
      (settings.waterAuthoring != nullptr && settings.waterAuthoring->authored) ||
      settings.quality != TerrainQuality::Standard)
    throw std::invalid_argument(
        "Graph terrain controls erosion, water and quality; pipeline overrides "
        "are unsupported");
  const auto graph = TerrainGraph::parse(recipe.graph);
  const auto graphProgress = [&](float value) {
    if (progress)
      progress(0.75F * value);
  };
  auto field = executeTerrainGraph(
      recipe, {settings.palette, {}, {}}, stop, graphProgress);
  if (!field)
    return std::nullopt;

  TerrainGenerationResult result;
  result.quality = TerrainQuality::Standard;
  result.water = settings.water;
  if (!result.water.authored)
    result.water.seaLevel = TerrainRuleContextBuilder::seaLevel(recipe);
  if (!std::isfinite(result.water.seaLevel))
    return std::nullopt;
  const auto &artifacts = *field->graphArtifacts;
  recordGraphStages(result, graph, artifacts);
  result.waterResult = artifacts.waterResult;

  std::optional<TerrainDrainage> drainage;
  if (settings.drainage || result.water.authored) {
    TerrainDrainageSettings drainageSettings;
    drainageSettings.seaLevel = result.water.seaLevel;
    drainage = computeTerrainDrainage(*field, recipe, drainageSettings, stop);
    if (!drainage)
      return std::nullopt;
  }
  if (!buildMasks(*field, recipe, drainage ? &*drainage : nullptr, nullptr,
                  result.masks, settings.drainage, stop))
    return std::nullopt;
  record(result, TerrainStage::Masks);
  field->stageOrder = "terrain_graph";
  for (const auto stage : result.stages) {
    field->stageOrder += ',';
    field->stageOrder += terrainStageName(stage);
  }
  if (stop.stop_requested())
    return std::nullopt;
  result.field = std::make_shared<const HeightField>(std::move(*field));
  result.fingerprint = fingerprintOf(result.stages, result.quality);
  if (result.fingerprint == 0)
    result.fingerprint = 1;
  if (progress)
    progress(1.F);
  if (stop.stop_requested())
    return std::nullopt;
  return result;
}

} // namespace

std::optional<TerrainGenerationResult>
generateTerrainStages(const TerrainRecipe &recipe,
                      const TerrainPipelineSettings &settings,
                      std::stop_token stop, const TerrainProgress &progress) {
  if (settings.waterError != nullptr)
    settings.waterError->clear();
  if (!recipe.graph.is_null())
    return generateGraphStages(recipe, settings, stop, progress);
  const auto baseProgress = [&](float value) {
    if (progress)
      progress(0.4F * value);
  };
  auto field = TerrainGenerator::generateBase(recipe, stop, baseProgress);
  if (!field)
    return std::nullopt;
  field->quality = std::string(terrainQualityName(settings.quality));

  TerrainGenerationResult result;
  result.quality = settings.quality;
  record(result, TerrainStage::Landform);
  record(result, TerrainStage::Surface);

  // One sea level for the whole pipeline. Derived once from the recipe's
  // landforms, or taken from the caller when the project authored one, because
  // two stages deriving it separately is how a shoreline ends up in two places
  // with different answers.
  const bool hasAuthoredWater = settings.waterAuthoring != nullptr &&
                                settings.waterAuthoring->authored;
  result.water.seaLevel = settings.water.authored
                              ? settings.water.seaLevel
                              : hasAuthoredWater
                                    ? settings.waterAuthoring->seaLevel
                                    : TerrainRuleContextBuilder::seaLevel(recipe);
  result.water.authored = settings.water.authored || hasAuthoredWater;
  if (!std::isfinite(result.water.seaLevel))
    return std::nullopt;

  std::optional<TerrainDrainage> initialDrainage;
  TerrainSamples<float> sediment;
  if (settings.drainage) {
    TerrainDrainageSettings drainage;
    drainage.seaLevel = result.water.seaLevel;
    drainage.quality = settings.quality;
    initialDrainage = computeTerrainDrainage(*field, recipe, drainage, stop);
    if (!initialDrainage)
      return std::nullopt;
    record(result, TerrainStage::Drainage);
    if (settings.erosion && !initialDrainage->flow.empty()) {
      TerrainErosionSettings erosion;
      erosion.quality = settings.quality;
      erosion.seed = recipe.seed;
      auto eroded = applyTerrainErosion(*field, *initialDrainage, erosion,
                                       &sediment, stop);
      if (stop.stop_requested())
        return std::nullopt;
      if (eroded) {
        for (std::size_t index = 0; index < eroded->size(); ++index) {
          if ((index & 255) == 0 && stop.stop_requested())
            return std::nullopt;
          field->heights.set(index, (*eroded)[index]);
        }
        record(result, TerrainStage::Erosion);
      }
    }
  }

  if (hasAuthoredWater) {
    TerrainMasks flowMask;
    if (initialDrainage)
      flowMask.flow = initialDrainage->flow;
    auto carved = carveTerrainWater(
        *field, *settings.waterAuthoring,
        initialDrainage ? &flowMask : nullptr, stop);
    if (stop.stop_requested())
      return std::nullopt;
    if (!carved)
      return std::nullopt;
    for (std::size_t index = 0; index < carved->carvedHeights.size(); ++index) {
      if ((index & 255) == 0 && stop.stop_requested())
        return std::nullopt;
      field->heights.set(index, carved->carvedHeights[index]);
    }
    // Rejected bodies remain visible to the caller while accepted ones run.
    if (carved->dropped > 0 && settings.waterError != nullptr)
      *settings.waterError = "Terrain water: " + std::to_string(carved->dropped) +
                             " authored bodies could not be placed.";
    record(result, TerrainStage::Hydrology);
    result.waterResult = std::move(carved);
  }

  const auto surfaceProgress = [&](float value) {
    if (progress)
      progress(0.4F + 0.35F * value);
  };
  if (!TerrainGenerator::applyTerrainSurfaceLayers(*field, recipe, stop,
                                                   surfaceProgress))
    return std::nullopt;
  record(result, TerrainStage::Sculpt);

  // Erosion and carving change the drainage surface; authored strokes can
  // change it again. Only final drainage may feed flow and water-distance masks.
  std::optional<TerrainDrainage> finalDrainage;
  const bool surfaceMayChange =
      terrainStageRan(result, TerrainStage::Erosion) ||
      terrainStageRan(result, TerrainStage::Hydrology) || !recipe.edits.empty();
  if (initialDrainage && !surfaceMayChange) {
    finalDrainage = std::move(initialDrainage);
  } else if (settings.drainage || result.water.authored) {
    TerrainDrainageSettings drainage;
    drainage.seaLevel = result.water.seaLevel;
    drainage.quality = settings.quality;
    finalDrainage = computeTerrainDrainage(*field, recipe, drainage, stop);
    if (!finalDrainage)
      return std::nullopt;
  }
  TerrainMasks masks;
  if (buildMasks(*field, recipe, finalDrainage ? &*finalDrainage : nullptr,
                 sediment.empty() ? nullptr : &sediment, masks,
                 settings.drainage, stop)) {
    result.masks = std::move(masks);
    record(result, TerrainStage::Masks);
  } else
    return std::nullopt;

  field->stageOrder.clear();
  for (const auto stage : result.stages) {
    if (!field->stageOrder.empty())
      field->stageOrder += ',';
    field->stageOrder += terrainStageName(stage);
  }

  if (result.quality == TerrainQuality::Standard &&
      !recipe.paletteId.empty() && settings.palette != nullptr) {
    if (stop.stop_requested())
      return std::nullopt;
    // One combined answer to "may something be placed here", so the editor's
    // explanation and the solver's decision cannot disagree.
    const auto constraints = TerrainScatterConstraints::build(
        *field, recipe, result.masks.empty() ? nullptr : &result.masks,
        result.water);
    auto scattered = scatterTerrain(*field, recipe, *settings.palette, {},
                                    &constraints);
    field->scatterPlacements = std::move(scattered.placements);
    field->scatterTruncated = scattered.truncated;
    field->paletteId = settings.palette->id;
  }

  if (stop.stop_requested())
    return std::nullopt;

  result.field = std::make_shared<const HeightField>(std::move(*field));
  result.fingerprint = fingerprintOf(result.stages, result.quality);
  if (result.fingerprint == 0)
    result.fingerprint = 1;
  if (progress)
    progress(1.F);
  if (stop.stop_requested())
    return std::nullopt;
  return result;
}

std::string describeTerrainStages(const TerrainGenerationResult &result) {
  std::ostringstream text;
  text << terrainQualityName(result.quality) << " [";
  for (std::size_t index = 0; index < result.stages.size(); ++index) {
    if (index != 0)
      text << ',';
    text << terrainStageName(result.stages[index]);
  }
  text << "]";
  if (result.field != nullptr)
    text << " sea=" << result.water.seaLevel
         << " masks=" << (result.masks.empty() ? "none" : "present");
  return text.str();
}

} // namespace demi::runtime
