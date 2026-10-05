#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainDrainage.h"
#include "demi/runtime/terrain/TerrainErosion.h"
#include "demi/runtime/terrain/TerrainPipeline.h"
#include "demi/runtime/terrain/TerrainWater.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stop_token>
#include <string>

using namespace demi::runtime;

namespace {
TerrainRecipe recipe() {
  TerrainRecipe r;
  r.size = {64, 64};
  r.cellsX = r.cellsZ = 32;
  r.landforms.at("default").baseHeight = 8;
  r.landforms.at("default").heightVariation = 12;
  r.landforms.emplace("rock",
                      TerrainLandform{.baseHeight = 24, .heightVariation = 6});
  r.biomes.emplace("meadow", TerrainBiome{.landform = "default"});
  r.biomes.emplace("rock", TerrainBiome{.landform = "rock"});
  return r;
}

// The stages that always run, in order.
void baseStagesAlwaysRun() {
  const auto result = generateTerrainStages(recipe());
  assert(result.has_value());
  assert(result->field != nullptr);
  assert(terrainStageRan(*result, TerrainStage::Landform));
  assert(terrainStageRan(*result, TerrainStage::Surface));
  assert(terrainStageRan(*result, TerrainStage::Sculpt));
  // Drainage is on by default, so it must have run rather than being implied.
  assert(terrainStageRan(*result, TerrainStage::Drainage));
  assert(terrainStageRan(*result, TerrainStage::Masks));
  // A stage that did not run must be reported as absent, not as a zero mask.
  assert(!terrainStageRan(*result, TerrainStage::Erosion));
  assert(!terrainStageRan(*result, TerrainStage::Hydrology));
  assert(result->stages.front() == TerrainStage::Landform);
  const auto lastDerived = result->stages.size() - 1;
  assert(result->stages[lastDerived] == TerrainStage::Masks);
  // Every reported stage follows the declared order.
  for (const auto stage : result->stages)
    assert(std::find(terrainStageOrder.begin(), terrainStageOrder.end(),
                     stage) != terrainStageOrder.end());
  assert(std::is_sorted(result->stages.begin(), result->stages.end(),
                        [](TerrainStage a, TerrainStage b) {
                          return std::find(terrainStageOrder.begin(),
                                           terrainStageOrder.end(), a) <
                                 std::find(terrainStageOrder.begin(),
                                           terrainStageOrder.end(), b);
                        }));
}

// The whole point of the result type: a caller asks rather than assumes.
void stageAbsenceIsHonest() {
  TerrainPipelineSettings off;
  off.drainage = false;
  const auto without = generateTerrainStages(recipe(), off);
  assert(without.has_value());
  assert(!terrainStageRan(*without, TerrainStage::Drainage));
  // Masks are still derived, because they do not need drainage.
  assert(terrainStageRan(*without, TerrainStage::Masks));
  assert(!without->masks.empty());
  // And the field still generated, so switching drainage off is not fatal.
  assert(without->field != nullptr);
}

// Two results of the same recipe must be distinguishable only by what actually
// differed, and identical otherwise.
void resultsAreReproducible() {
  const auto first = generateTerrainStages(recipe());
  const auto second = generateTerrainStages(recipe());
  assert(first && second);
  assert(first->fingerprint == second->fingerprint);
  assert(first->stages == second->stages);
  assert(first->field->heights == second->field->heights);
  assert(first->water.seaLevel == second->water.seaLevel);
}

// A preview is NOT the same terrain as a standard result, and the fingerprint
// is what stops them being treated as interchangeable.
void previewDiffersFromStandard() {
  TerrainPipelineSettings preview;
  preview.quality = TerrainQuality::Preview;
  const auto cheap = generateTerrainStages(recipe(), preview);
  const auto full = generateTerrainStages(recipe());
  assert(cheap && full);
  assert(cheap->quality == TerrainQuality::Preview);
  assert(full->quality == TerrainQuality::Standard);
  assert(cheap->fingerprint != full->fingerprint);
  assert(describeTerrainStages(*cheap).find("preview") != std::string::npos);
  assert(describeTerrainStages(*full).find("standard") != std::string::npos);
}

// Sea level must be the number the rules use, or a shoreline and a water mask
// would disagree about where the water is.
void seaLevelMatchesTheRules() {
  const auto result = generateTerrainStages(recipe());
  assert(result.has_value());
  assert(result->water.seaLevel ==
         TerrainRuleContextBuilder::seaLevel(recipe()));

  // An authored level wins over the derived one, everywhere.
  TerrainPipelineSettings authored;
  authored.water = TerrainWaterLevel{.seaLevel = 3.5F, .authored = true};
  const auto pinned = generateTerrainStages(recipe(), authored);
  assert(pinned.has_value());
  assert(pinned->water.seaLevel == 3.5F);
  assert(pinned->water.authored);
  for (float level : {0.F, -5.F}) {
    auto flat = recipe();
    flat.landforms.at("default").baseHeight = level + 1.F;
    flat.landforms.at("default").heightVariation = 0.F;
    flat.landforms.at("rock").baseHeight = 24.F;
    flat.landforms.at("rock").heightVariation = 0.F;
    authored.water.seaLevel = level;
    const auto result = generateTerrainStages(flat, authored);
    assert(result);
    assert(result->water.seaLevel == level);
    for (std::size_t index = 0; index < result->field->heights.size();
         ++index) {
      assert(result->field->heights[index] > level);
      assert(result->masks.waterDistance[index] > 0.F);
      assert(result->masks.substrate[index] !=
             static_cast<std::size_t>(TerrainSubstrate::Wet));
    }
  }
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  authoring.seaLevel = -3.F;
  TerrainPipelineSettings fromDocument;
  fromDocument.waterAuthoring = &authoring;
  const auto documentLevel = generateTerrainStages(recipe(), fromDocument);
  assert(documentLevel && documentLevel->water.authored);
  assert(documentLevel->water.seaLevel == -3.F);
}

// Masks must describe the surface the world will show.
void masksDescribeTheGeneratedSurface() {
  const auto result = generateTerrainStages(recipe());
  assert(result.has_value());
  const auto &masks = result->masks;
  const auto count = result->field->heights.size();
  assert(masks.count() == count);
  for (std::size_t index = 0; index < count; ++index) {
    const float slope = masks.slope[index];
    assert(std::isfinite(slope) && slope >= 0.F && slope <= 90.F);
    const float moisture = masks.moisture[index];
    assert(std::isfinite(moisture) && moisture >= 0.F && moisture <= 1.F);
    const float distance = masks.waterDistance[index];
    assert(std::isfinite(distance) && distance >= 0.F);
    // Water distance is zero exactly in water.
    const bool wet = result->field->heights[index] <= result->water.seaLevel;
    if (wet)
      assert(distance == 0.F);
  }
}

// Flow must reach the masks, which is the visible sign the drainage stage fed
// the pipeline rather than running beside it.
void flowReachesTheMasks() {
  const auto result = generateTerrainStages(recipe());
  assert(result.has_value());
  std::size_t flowing = 0;
  for (std::size_t index = 0; index < result->masks.flow.size(); ++index)
    if (result->masks.flow[index] > 0.F)
      ++flowing;
  assert(flowing > 0);

  // With drainage off there is no flow, and the mask says zero rather than
  // carrying a stale value from somewhere else.
  TerrainPipelineSettings off;
  off.drainage = false;
  const auto without = generateTerrainStages(recipe(), off);
  assert(without.has_value());
  for (std::size_t index = 0; index < without->masks.flow.size(); ++index)
    assert(without->masks.flow[index] == 0.F);
}

void finalDrainageMatchesDisplayedSurface() {
  auto sculpted = recipe();
  TerrainEdit edit;
  edit.kind = TerrainEditKind::Raise;
  edit.center = {32, 32};
  edit.radius = 12;
  edit.amount = 20;
  sculpted.edits.push_back(edit);
  TerrainPipelineSettings settings;
  settings.erosion = true;
  const auto result = generateTerrainStages(sculpted, settings);
  assert(result);
  TerrainDrainageSettings drainage;
  drainage.seaLevel = result->water.seaLevel;
  const auto final = computeTerrainDrainage(*result->field, sculpted, drainage);
  assert(final);
  assert(result->masks.flow == final->flow);
  assert(result->masks.waterDistance == final->waterDistance);
}

// Erosion is opt-in and changes the processed surface, not retained landform.
void erosionIsOptInAndChangesOnlyTheSurface() {
  TerrainPipelineSettings eroded;
  eroded.erosion = true;
  const auto plain = generateTerrainStages(recipe());
  const auto worn = generateTerrainStages(recipe(), eroded);
  assert(plain && worn);
  assert(!terrainStageRan(*plain, TerrainStage::Erosion));
  assert(terrainStageRan(*worn, TerrainStage::Erosion));
  assert(plain->field->heights != worn->field->heights);
  assert(plain->field->baseHeights == worn->field->baseHeights);
  bool hasTransport = false;
  for (std::size_t index = 0; index < worn->masks.sediment.size(); ++index)
    hasTransport = hasTransport || worn->masks.sediment[index] != 0.F;
  assert(hasTransport);
}

void erosionPrecedesAuthoredStrokes() {
  auto sculpted = recipe();
  TerrainEdit edit;
  edit.kind = TerrainEditKind::Raise;
  edit.center = {32, 32};
  edit.radius = 6;
  edit.amount = 4;
  sculpted.edits.push_back(edit);

  TerrainPipelineSettings eroded;
  eroded.erosion = true;
  const auto worn = generateTerrainStages(sculpted, eroded);
  assert(worn);
  assert(terrainStageRan(*worn, TerrainStage::Erosion));
  const auto plain = generateTerrainStages(sculpted);
  assert(plain);
  const auto unsculpted = generateTerrainStages(recipe(), eroded);
  assert(unsculpted);
  assert(worn->field->baseHeights == plain->field->baseHeights);
  const auto center = worn->field->index(16, 16);
  assert(std::abs(worn->field->heights[center] -
                  unsculpted->field->heights[center] - 4.F) < 0.01F);
  assert(worn->field->heights != plain->field->heights);
}

void sharedGeneratorPassesMatchGenerate() {
  auto authored = recipe();
  TerrainEdit edit;
  edit.kind = TerrainEditKind::Raise;
  edit.center = {32, 32};
  edit.radius = 6;
  edit.amount = 4;
  authored.edits.push_back(edit);
  auto base = TerrainGenerator::generateBase(authored);
  const auto full = TerrainGenerator::generate(authored);
  assert(base && full);
  const auto originalBase = base->baseHeights;
  assert(base->heights == base->baseHeights);
  assert(base->exclusions.empty());
  assert(TerrainGenerator::applyTerrainSurfaceLayers(*base, authored));
  assert(base->baseHeights == originalBase);
  assert(base->heights == full->heights);
  assert(base->normals.size() == full->normals.size());
  for (std::size_t index = 0; index < base->normals.size(); ++index) {
    const auto a = base->normals[index];
    const auto b = full->normals[index];
    assert(a.x == b.x && a.y == b.y && a.z == b.z);
  }
  assert(base->exclusions == full->exclusions);
}

// Water is a stage, so it must be reported as one, and it must change the
// ground it is authored on.
void waterStageCarvesAndIsReported() {
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  TerrainWaterBodySpec lake;
  lake.id = "lake";
  lake.kind = TerrainWaterBody::Lake;
  lake.level = 2;
  lake.radius = 0; // fill every basin below the level
  authoring.bodies.push_back(lake);

  TerrainPipelineSettings settings;
  settings.waterAuthoring = &authoring;
  settings.water = TerrainWaterLevel{.seaLevel = 6, .authored = false};
  std::string problem;
  settings.waterError = &problem;

  const auto dry = generateTerrainStages(recipe());
  const auto wet = generateTerrainStages(recipe(), settings);
  assert(dry && wet);
  assert(!terrainStageRan(*dry, TerrainStage::Hydrology));
  assert(terrainStageRan(*wet, TerrainStage::Hydrology));
  assert(wet->waterResult);
  assert(wet->waterResult->surfaces.size() == 1);
  assert(!wet->waterResult->surfaces.front().vertices.empty());
  assert(problem.empty());
  // The authored water carved the surface, so the ground is not the same.
  assert(wet->field->heights != dry->field->heights);
  // An authored body is not the same as no water authoring: the stage ran.
  assert(describeTerrainStages(*wet).find("hydrology") != std::string::npos);
  assert(describeTerrainStages(*dry).find("hydrology") == std::string::npos);
}

void sculptingFollowsWaterCarving() {
  auto sculpted = recipe();
  TerrainEdit edit;
  edit.kind = TerrainEditKind::Raise;
  edit.center = {32, 32};
  edit.radius = 6;
  edit.amount = 4;
  sculpted.edits.push_back(edit);
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  authoring.seaLevel = 2;
  authoring.bodies.push_back(TerrainWaterBodySpec{
      .id = "lake", .kind = TerrainWaterBody::Lake, .level = 30});
  TerrainPipelineSettings settings;
  settings.waterAuthoring = &authoring;
  const auto result = generateTerrainStages(sculpted, settings);
  assert(result && result->waterResult);
  const auto center = result->field->index(16, 16);
  assert(std::abs(result->field->heights[center] -
                  result->waterResult->carvedHeights[center] - 4.F) < 0.01F);
  assert(result->field->baseHeights[center] !=
         result->waterResult->carvedHeights[center]);
}

void cancellationPropagatesThroughStages() {
  std::stop_source source;
  source.request_stop();
  assert(!TerrainGenerator::generateBase(recipe(), source.get_token()));
  assert(!generateTerrainStages(recipe(), {}, source.get_token()));
  auto field = TerrainGenerator::generateBase(recipe());
  assert(field);
  assert(!TerrainGenerator::applyTerrainSurfaceLayers(*field, recipe(),
                                                      source.get_token()));
  assert(
      !TerrainGenerator::recomputeTerrainNormals(*field, source.get_token()));
  TerrainDrainageSettings drainage;
  assert(
      !computeTerrainDrainage(*field, recipe(), drainage, source.get_token()));
  const auto drained = computeTerrainDrainage(*field, recipe(), drainage);
  assert(drained);
  assert(
      !applyTerrainErosion(*field, *drained, {}, nullptr, source.get_token()));
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  assert(!carveTerrainWater(*field, authoring, nullptr, source.get_token()));

  std::stop_source duringSurface;
  const auto cancelled = generateTerrainStages(
      recipe(), {}, duringSurface.get_token(), [&](float progress) {
        if (progress >= 0.5F)
          duringSurface.request_stop();
      });
  assert(!cancelled);
}

// A body that could not be placed must be surfaced, not silently dropped into
// waterless land.
void rejectedWaterBodiesAreReported() {
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  TerrainWaterBodySpec bad;
  bad.id = ""; // an empty id is rejected by the carve
  bad.kind = TerrainWaterBody::Lake;
  bad.level = 2;
  authoring.bodies.push_back(bad);

  TerrainPipelineSettings settings;
  settings.waterAuthoring = &authoring;
  settings.water = TerrainWaterLevel{.seaLevel = 6, .authored = false};
  std::string problem;
  settings.waterError = &problem;

  const auto result = generateTerrainStages(recipe(), settings);
  assert(result.has_value());
  // The stage still ran for the bodies that were usable, and the problem is
  // reported rather than swallowed.
  assert(!problem.empty());
  assert(problem.find("could not be placed") != std::string::npos);
}
} // namespace

int main() {
  baseStagesAlwaysRun();
  stageAbsenceIsHonest();
  resultsAreReproducible();
  previewDiffersFromStandard();
  seaLevelMatchesTheRules();
  masksDescribeTheGeneratedSurface();
  flowReachesTheMasks();
  finalDrainageMatchesDisplayedSurface();
  erosionIsOptInAndChangesOnlyTheSurface();
  erosionPrecedesAuthoredStrokes();
  sharedGeneratorPassesMatchGenerate();
  waterStageCarvesAndIsReported();
  sculptingFollowsWaterCarving();
  rejectedWaterBodiesAreReported();
  cancellationPropagatesThroughStages();
  std::cout << "Terrain pipeline checks passed\n";
}
