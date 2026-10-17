#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainGraphNodeEvaluation.h"
#include "demi/runtime/terrain/TerrainGraphSpatialMasks.h"
#include "demi/runtime/terrain/TerrainPipeline.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainWaterGeometry.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace demi::runtime;

namespace {
TerrainRecipe recipe() {
  TerrainRecipe value;
  value.size = {16, 16};
  value.cellsX = value.cellsZ = 16;
  value.chunkCells = 8;
  value.landforms.at("default").baseHeight = 3;
  value.landforms.at("default").heightVariation = 0;
  value.landforms.emplace("high", TerrainLandform{.baseHeight = 100});
  value.biomes.emplace("high", TerrainBiome{.landform = "high"});
  value.graph = defaultTerrainGraph();
  return value;
}

bool wasCached(const HeightField &field, std::string_view node) {
  for (const auto &run : field.graphArtifacts->nodes)
    if (run.id == node)
      return run.cached;
  assert(false && "Graph node was not executed");
  return false;
}

std::shared_ptr<const HeightField> retained(HeightField field) {
  return std::make_shared<const HeightField>(std::move(field));
}

void paintedBiomesKeepGraphHeights() {
  const auto before = recipe();
  const auto first = executeTerrainGraph(before);
  assert(first && first->graphArtifacts && first->graphArtifacts->baseField);
  assert(!first->graphArtifacts->baseField->graphArtifacts);
  const auto center = first->index(8, 8);
  assert(first->height(8, 8) == 3.F);

  auto after = before;
  TerrainRegion region;
  region.biome = "high";
  region.center = {8, 8};
  region.radius = 3;
  region.falloff = 0;
  after.regions.push_back(region);
  const auto previous = retained(*first);
  const auto update = updateTerrain(before, after, previous);
  assert(update && !update->invalidation.fullGeneration);
  assert(update->invalidation.heightSamples.empty());
  assert(update->invalidation.baseSamples.empty());
  assert(!update->invalidation.biomeSamples.empty());
  assert(update->field->heights == previous->heights);
  assert(update->field->baseHeights == previous->baseHeights);
  assert(update->field->biomeIndices[center] == 1);
  const auto full = executeTerrainGraph(after);
  assert(full && full->heights == update->field->heights);
  assert(full->biomeIndices == update->field->biomeIndices);
  const auto undone = applyTerrainPatch(update->field, *update->patch, false);
  assert(undone.field->biomeIndices == previous->biomeIndices);

  auto overpainted = after;
  region.biome = "default";
  overpainted.regions.push_back(region);
  const auto secondStroke = updateTerrain(after, overpainted, update->field);
  assert(secondStroke && !secondStroke->invalidation.fullGeneration);
  assert(secondStroke->field->biomeIndices[center] == 0);
  assert(secondStroke->field->heights == previous->heights);
  const auto cached = executeTerrainGraph(
      overpainted, {nullptr, {}, full->graphArtifacts->cache});
  assert(cached && wasCached(*cached, "landform"));
  assert(cached->biomeIndices == secondStroke->field->biomeIndices);

  TerrainEdit sculpt;
  sculpt.kind = TerrainEditKind::Raise;
  sculpt.center = {8, 8};
  sculpt.radius = 2;
  sculpt.amount = 4;
  const auto painted = after;
  after.edits.push_back(sculpt);
  const auto edited = updateTerrain(painted, after, update->field);
  assert(edited && !edited->invalidation.fullGeneration);
  assert(edited->field->heights[center] == 7.F);
  assert(edited->field->baseHeights[center] == 3.F);
  const auto fullyEdited = executeTerrainGraph(after);
  assert(fullyEdited);
  assert(edited->field->heights == fullyEdited->heights);
}

void cacheIgnoresAppearanceAndRetainsRuleBoundary() {
  auto before = recipe();
  before.graph["nodes"].push_back({{"id", "rules"}, {"type", "biomes"}});
  before.graph["links"][0]["from"]["node"] = "rules";
  before.graph["links"].push_back(
      {{"id", "landform_rules"},
       {"from", {{"node", "landform"}, {"port", "field"}}},
       {"to", {{"node", "rules"}, {"port", "field"}}}});
  TerrainBiomeRule rule;
  rule.id = "always_high";
  rule.biome = "high";
  before.rules.push_back(rule);
  auto first = executeTerrainGraph(before);
  assert(first && first->biomeIndices[first->index(8, 8)] == 1);

  auto appearance = before;
  appearance.biomes.at("high").color = {.7F, .2F, .1F, 1.F};
  appearance.biomes.at("high").material = "asset://materials/high";
  appearance.biomes.at("high").textureScale = 4;
  appearance.paletteId = "asset://palettes/one";
  appearance.presetId = "asset://presets/one";
  appearance.presetVersion = 1;
  appearance.chunkCells = 4;
  auto refreshed = executeTerrainGraph(
      appearance, {nullptr, "palette-v2", first->graphArtifacts->cache});
  assert(refreshed);
  assert(wasCached(*refreshed, "landform"));
  assert(wasCached(*refreshed, "rules"));
  assert(wasCached(*refreshed, "terrain_output"));
  assert(refreshed->biomeColors[1].r == .7F);
  assert(refreshed->biomeMaterials[1] == "asset://materials/high");
  assert(refreshed->biomeTextureScales[1] == 4);
  assert(refreshed->chunks.front().cellsX == 4);

  auto changedRule = appearance;
  changedRule.rules[0].biome = "default";
  auto rerun = executeTerrainGraph(
      changedRule, {nullptr, "palette-v2", refreshed->graphArtifacts->cache});
  assert(rerun);
  assert(wasCached(*rerun, "landform"));
  assert(!wasCached(*rerun, "rules"));
  assert(!wasCached(*rerun, "terrain_output"));
  assert(rerun->biomeIndices[rerun->index(8, 8)] == 0);

  TerrainRegion painted;
  painted.biome = "high";
  painted.center = {8, 8};
  painted.radius = 2;
  painted.falloff = 0;
  changedRule.regions.push_back(painted);
  auto overlay = executeTerrainGraph(
      changedRule, {nullptr, "palette-v2", rerun->graphArtifacts->cache});
  assert(overlay);
  assert(wasCached(*overlay, "landform"));
  assert(wasCached(*overlay, "rules"));
  assert(wasCached(*overlay, "terrain_output"));
  assert(
      overlay->graphArtifacts->baseField->biomeIndices[overlay->index(8, 8)] ==
      0);
  assert(overlay->biomeIndices[overlay->index(8, 8)] == 1);
  assert(overlay->heights == rerun->heights);

  auto reshaped = changedRule;
  reshaped.landforms.at("default").baseHeight = 5;
  const auto newBase = executeTerrainGraph(
      reshaped, {nullptr, "palette-v2", overlay->graphArtifacts->cache});
  assert(newBase && !wasCached(*newBase, "landform"));
  assert(newBase->height(8, 8) == 5.F);
}

void paletteFingerprintOnlyInvalidatesScatter() {
  auto authored = recipe();
  authored.paletteId = "asset://palettes/one";
  authored.graph["nodes"].push_back({{"id", "placement"}, {"type", "scatter"}});
  authored.graph["links"].push_back(
      {{"id", "landform_placement"},
       {"from", {{"node", "landform"}, {"port", "field"}}},
       {"to", {{"node", "placement"}, {"port", "field"}}}});
  authored.graph["links"].push_back(
      {{"id", "placement_output"},
       {"from", {{"node", "placement"}, {"port", "instances"}}},
       {"to", {{"node", "terrain_output"}, {"port", "instances"}}}});
  TerrainPalette palette;
  palette.id = authored.paletteId;
  auto first = executeTerrainGraph(authored, {&palette, "version-one", {}});
  assert(first);
  auto second = executeTerrainGraph(
      authored, {&palette, "version-two", first->graphArtifacts->cache});
  assert(second);
  assert(wasCached(*second, "landform"));
  assert(!wasCached(*second, "placement"));
  assert(!wasCached(*second, "terrain_output"));

  const auto changedPalette = std::make_shared<const TerrainPalette>(palette);
  const TerrainGenerationInputs changedInputs{changedPalette, "version-two"};
  const auto updated = updateTerrainWithInputs(authored, authored,
                                               retained(*first), changedInputs);
  assert(updated && updated->invalidation.fullGeneration);
  assert(wasCached(*updated->field, "landform"));
  assert(!wasCached(*updated->field, "placement"));
  assert(updated->field->inputFingerprint == "version-two");

  auto withoutPalette = authored;
  withoutPalette.paletteId.clear();
  const auto removed = updateTerrainWithInputs(
      authored, withoutPalette, updated->field, TerrainGenerationInputs{});
  assert(removed && removed->invalidation.fullGeneration);
  assert(wasCached(*removed->field, "landform"));
  assert(!wasCached(*removed->field, "placement"));
  assert(removed->field->paletteId.empty());
  assert(removed->field->inputFingerprint.empty());
  assert(!removed->field->resolvedPalette);
  assert(removed->field->scatterPlacements.empty());
}

void rewiredSourceInvalidatesOutput() {
  auto authored = recipe();
  authored.graph["nodes"][0]["type"] = "constant";
  authored.graph["nodes"][0]["parameters"] = {{"height", 3}};
  authored.graph["nodes"].push_back({{"id", "alternate"},
                                     {"type", "constant"},
                                     {"parameters", {{"height", 3}}}});
  const auto first = executeTerrainGraph(authored);
  assert(first);
  authored.graph["links"][0]["from"]["node"] = "alternate";
  const auto rewired = executeTerrainGraph(
      authored, {nullptr, {}, first->graphArtifacts->cache});
  assert(rewired);
  assert(!wasCached(*rewired, "terrain_output"));
  assert(rewired->heights == first->heights);
}

void executionOrderFollowsOnlyActiveOutput() {
  auto graph = defaultTerrainGraph();
  graph["nodes"].push_back({{"id", "unused"}, {"type", "constant"}});
  graph["nodes"].push_back({{"id", "offset"}, {"type", "offset"}});
  graph["links"][0]["from"]["node"] = "offset";
  graph["links"].push_back({{"id", "landform_offset"},
                            {"from", {{"node", "landform"}, {"port", "field"}}},
                            {"to", {{"node", "offset"}, {"port", "field"}}}});
  const auto parsed = TerrainGraph::parse(graph);
  assert((parsed.executionOrder() ==
          std::vector<std::string>{"landform", "offset", "terrain_output"}));

  // A cycle in an unused branch still invalidates the authored graph.
  graph["nodes"].push_back({{"id", "unused_offset"}, {"type", "offset"}});
  graph["links"].push_back(
      {{"id", "unused_cycle"},
       {"from", {{"node", "unused_offset"}, {"port", "field"}}},
       {"to", {{"node", "unused_offset"}, {"port", "field"}}}});
  const auto diagnostics = terrainGraphDiagnostics(graph);
  assert(!diagnostics.empty());
  assert(diagnostics.front().message == "Terrain graph contains a cycle");
}

void branchesAndPipelineUseGraphOutput() {
  auto authored = recipe();
  authored.graph = {
      {"format_version", 1},
      {"output", "terrain_output"},
      {"nodes",
       {{{"id", "low"}, {"type", "constant"}, {"parameters", {{"height", 3}}}},
        {{"id", "high"}, {"type", "constant"}, {"parameters", {{"height", 7}}}},
        {{"id", "blend"},
         {"type", "height_blend"},
         {"parameters", {{"amount", 0.25}}}},
        {{"id", "terrain_output"}, {"type", "output"}}}},
      {"links",
       {{{"id", "low_blend"},
         {"from", {{"node", "low"}, {"port", "field"}}},
         {"to", {{"node", "blend"}, {"port", "a"}}}},
        {{"id", "high_blend"},
         {"from", {{"node", "high"}, {"port", "field"}}},
         {"to", {{"node", "blend"}, {"port", "b"}}}},
        {{"id", "blend_output"},
         {"from", {{"node", "blend"}, {"port", "field"}}},
         {"to", {{"node", "terrain_output"}, {"port", "field"}}}}}}};
  TerrainEdit edit;
  edit.kind = TerrainEditKind::Raise;
  edit.center = {8, 8};
  edit.radius = 2;
  edit.amount = 4;
  authored.edits.push_back(edit);
  const auto direct = executeTerrainGraph(authored);
  const auto staged = generateTerrainStages(authored);
  assert(direct && staged);
  assert(direct->height(8, 8) == 8.F);
  assert(staged->field->heights == direct->heights);
  assert(staged->field->baseHeights == direct->baseHeights);
  assert(!terrainStageRan(*staged, TerrainStage::Erosion));
  assert(!terrainStageRan(*staged, TerrainStage::Hydrology));
  assert(!terrainStageRan(*staged, TerrainStage::Drainage));
  assert(terrainStageRan(*staged, TerrainStage::Masks));
  assert(staged->masks.count() == direct->heights.size());

  TerrainPipelineSettings conflict;
  conflict.erosion = true;
  try {
    (void)generateTerrainStages(authored, conflict);
    assert(false && "Graph erosion override should be rejected");
  } catch (const std::invalid_argument &) {
  }

  TerrainWaterAuthoring water;
  water.authored = true;
  conflict.erosion = false;
  conflict.waterAuthoring = &water;
  try {
    (void)generateTerrainStages(authored, conflict);
    assert(false && "Graph water override should be rejected");
  } catch (const std::invalid_argument &) {
  }
}

void graphStageReportingFollowsNodes() {
  auto authored = recipe();
  authored.graph["nodes"].push_back(
      {{"id", "erode"},
       {"type", "erosion"},
       {"parameters", {{"iterations", 1}, {"thermal_strength", 0}}}});
  authored.graph["links"][0]["from"]["node"] = "erode";
  authored.graph["links"].push_back(
      {{"id", "landform_erode"},
       {"from", {{"node", "landform"}, {"port", "field"}}},
       {"to", {{"node", "erode"}, {"port", "field"}}}});
  const auto direct = executeTerrainGraph(authored);
  const auto staged = generateTerrainStages(authored);
  assert(direct && staged);
  assert(terrainStageRan(*staged, TerrainStage::Erosion));
  assert(staged->field->heights == direct->heights);
}

void emptyConnectedWaterRemainsAuthoritativeForRulesAndScatter() {
  using namespace terrain_graph_detail;
  auto authored = recipe();
  TerrainBiomeRule wetRule;
  wetRule.id = "wet_bed";
  wetRule.biome = "high";
  wetRule.substrate = {TerrainSubstrate::Wet};
  authored.rules.push_back(wetRule);
  const auto generated = executeTerrainGraph(authored);
  assert(generated);
  const auto field = retained(*generated);
  auto water = std::make_shared<GraphWater>();
  water->authoring.authored = true;
  water->data.carvedHeights = field->heights;
  const GraphValues connected{
      {"field", GraphField(field)},
      {"water", std::shared_ptr<const GraphWater>(water)}};
  const TerrainGraphNode rules{"rules", "biomes"};
  const auto ruled = evaluateTerrainGraphNode(rules, {}, authored, authored, {},
                                              connected, {});
  assert(ruled);
  const auto output = std::get<GraphField>(ruled->outputs.at("field"));
  for (std::size_t sample = 0; sample < output->heights.size(); ++sample) {
    assert(output->biomeIndices[sample] == 0);
  }
  TerrainPalette palette;
  palette.placements.emplace(
      "cover", TerrainPaletteEntry{
                   .model = "asset://cover", .weight = 1.F, .spacing = 0.F});
  const TerrainGraphNode scatter{"scatter", "scatter"};
  const auto scattered = evaluateTerrainGraphNode(
      scatter, {}, authored, authored, {.palette = &palette}, connected, {});
  assert(scattered);
  const auto instances = std::get<std::shared_ptr<const GraphInstances>>(
      scattered->outputs.at("instances"));
  assert(!instances->placements.empty());
  // The retained input water remains immutable; no mesh or cached coverage was
  // installed into an upstream result merely to answer a downstream rule.
  assert(!water->data.resolvedCoverage && water->data.surfaces.empty());

  TerrainWaterBodySpec sea;
  sea.id = "dry_sea";
  sea.kind = TerrainWaterBody::Ocean;
  sea.level = 2.F;
  sea.shorelineSoftening = false;
  water->authoring.bodies.push_back(sea);
  const auto retainedWater =
      refreshTerrainWaterResult(*field, water->authoring, field->heights);
  assert(retainedWater);
  water->data = *retainedWater;
  // Geometry coverage retains dry corners to interpolate shore triangles.
  assert(water->data.resolvedCoverage->wet.front() == 1);
  assert(field->heights[0] > water->data.resolvedCoverage->level[0]);
  const auto dryRuled = evaluateTerrainGraphNode(rules, {}, authored, authored,
                                                 {}, connected, {});
  assert(dryRuled);
  const auto dryField = std::get<GraphField>(dryRuled->outputs.at("field"));
  for (std::size_t sample = 0; sample < dryField->heights.size(); ++sample) {
    assert(dryField->biomeIndices[sample] == 0);
  }
  const auto dryScattered = evaluateTerrainGraphNode(
      scatter, {}, authored, authored, {.palette = &palette}, connected, {});
  assert(dryScattered);
  assert(!std::get<std::shared_ptr<const GraphInstances>>(
              dryScattered->outputs.at("instances"))
              ->placements.empty());

  std::stop_source cancelled;
  cancelled.request_stop();
  const auto cancelledCoverage = terrain_water_detail::buildWaterLevelField(
      field->size, field->cellsX, field->cellsZ, field->heights,
      water->authoring, cancelled.get_token());
  assert(cancelledCoverage.wet.empty());

  auto wrongGrid = std::make_shared<terrain_water_detail::WaterLevelField>(
      *water->data.resolvedCoverage);
  ++wrongGrid->cellsX;
  water->data.resolvedCoverage = std::move(wrongGrid);
  bool rejected = false;
  try {
    (void)evaluateTerrainGraphNode(rules, {}, authored, authored, {}, connected,
                                   {});
  } catch (const std::invalid_argument &error) {
    rejected =
        std::string(error.what()).find("input field grid") != std::string::npos;
  }
  assert(rejected);
}

void biomeRulesUseConnectedWaterInsteadOfInferredSea() {
  for (const auto *type : {"biomes", "scatter"}) {
    const auto *definition = terrainGraphNodeDefinition(type);
    assert(definition && definition->inputs.size() == 2);
    assert(definition->inputs.front().required);
    assert(definition->inputs[1].name == "water");
    assert(definition->inputs[1].kind == TerrainGraphValueKind::Water);
    assert(!definition->inputs[1].required);
  }
  for (const float inferredPeak : {0.F, 100.F}) {
    auto authored = recipe();
    authored.landforms.at("high").baseHeight = inferredPeak;
    authored.landforms.at("high").heightVariation = 0;
    TerrainBiomeRule wetRule;
    wetRule.id = "wet_bed";
    wetRule.biome = "high";
    wetRule.substrate = {TerrainSubstrate::Wet};
    authored.rules.push_back(wetRule);
    authored.graph["nodes"].push_back({{"id", "lake"},
                                       {"type", "water"},
                                       {"parameters",
                                        {{"kind", "lake"},
                                         {"level", 5},
                                         {"center_x", 8},
                                         {"center_z", 8},
                                         {"radius", 4}}}});
    authored.graph["nodes"].push_back({{"id", "rules"}, {"type", "biomes"}});
    authored.graph["links"] = nlohmann::json::array(
        {{{"id", "land_lake"},
          {"from", {{"node", "landform"}, {"port", "field"}}},
          {"to", {{"node", "lake"}, {"port", "field"}}}},
         {{"id", "lake_rules"},
          {"from", {{"node", "lake"}, {"port", "field"}}},
          {"to", {{"node", "rules"}, {"port", "field"}}}},
         {{"id", "rules_output"},
          {"from", {{"node", "rules"}, {"port", "field"}}},
          {"to", {{"node", "terrain_output"}, {"port", "field"}}}},
         {{"id", "lake_output"},
          {"from", {{"node", "lake"}, {"port", "water"}}},
          {"to", {{"node", "terrain_output"}, {"port", "water"}}}}});
    const auto inferred = executeTerrainGraph(authored);
    assert(inferred);
    const auto expectedInferred = inferredPeak > 0.F ? 1U : 0U;
    assert(inferred->biomeIndices[inferred->index(8, 8)] == expectedInferred);
    assert(inferred->biomeIndices[inferred->index(0, 0)] == expectedInferred);
    authored.graph["links"].push_back(
        {{"id", "water_rules"},
         {"from", {{"node", "lake"}, {"port", "water"}}},
         {"to", {{"node", "rules"}, {"port", "water"}}}});
    const auto resolved = executeTerrainGraph(
        authored, {.previousCache = inferred->graphArtifacts->cache});
    assert(resolved);
    assert(!wasCached(*resolved, "rules"));
    assert(resolved->biomeIndices[resolved->index(8, 8)] == 1);
    assert(resolved->biomeIndices[resolved->index(0, 0)] == 0);
    assert(resolved->heights == inferred->heights);

    authored.graph["nodes"].push_back({{"id", "scatter"}, {"type", "scatter"}});
    authored.graph["links"].push_back(
        {{"id", "rules_scatter"},
         {"from", {{"node", "rules"}, {"port", "field"}}},
         {"to", {{"node", "scatter"}, {"port", "field"}}}});
    authored.graph["links"].push_back(
        {{"id", "water_scatter"},
         {"from", {{"node", "lake"}, {"port", "water"}}},
         {"to", {{"node", "scatter"}, {"port", "water"}}}});
    authored.graph["links"].push_back(
        {{"id", "scatter_output"},
         {"from", {{"node", "scatter"}, {"port", "instances"}}},
         {"to", {{"node", "terrain_output"}, {"port", "instances"}}}});
    TerrainPalette palette;
    palette.placements.emplace(
        "cover", TerrainPaletteEntry{
                     .model = "asset://cover", .weight = 1.F, .spacing = 0.F});
    const auto scattered = executeTerrainGraph(authored, {.palette = &palette});
    assert(scattered && scattered->graphArtifacts->basePlacements);
    assert(!scattered->graphArtifacts->basePlacements->empty());
    const auto &wet =
        scattered->graphArtifacts->waterResult->resolvedCoverage->wet;
    for (const auto &placement : *scattered->graphArtifacts->basePlacements) {
      const auto x = static_cast<int>(placement.position.x);
      const auto z = static_cast<int>(placement.position.z);
      assert(wet[scattered->index(x, z)] == 0);
    }

    authored.graph["nodes"].push_back({{"id", "unrelated"},
                                       {"type", "constant"},
                                       {"parameters", {{"height", 30}}}});
    authored.graph["links"][1]["from"]["node"] = "unrelated";
    const auto dried = executeTerrainGraph(authored, {.palette = &palette});
    assert(dried && dried->height(8, 8) == 30.F);
    assert(dried->biomeIndices[dried->index(8, 8)] == 0);
    assert(dried->biomeIndices[dried->index(0, 0)] == 0);
    assert(dried->graphArtifacts->waterResult->resolvedCoverage
               ->wet[dried->index(8, 8)] == 0);
    authored.graph["nodes"].back()["parameters"]["height"] = 1;
    const auto flooded = executeTerrainGraph(authored, {.palette = &palette});
    assert(flooded && flooded->height(8, 8) == 1.F);
    assert(flooded->biomeIndices[flooded->index(8, 8)] == 1);
    assert(flooded->biomeIndices[flooded->index(0, 0)] == 0);
    assert(flooded->graphArtifacts->waterResult->resolvedCoverage
               ->wet[flooded->index(8, 8)] == 1);
  }
}

void graphWaterResultSurvivesPipeline() {
  auto authored = recipe();
  authored.graph["nodes"].push_back({{"id", "lake"},
                                     {"type", "water"},
                                     {"parameters",
                                      {{"kind", "lake"},
                                       {"level", 5},
                                       {"center_x", 8},
                                       {"center_z", 8},
                                       {"radius", 5}}}});
  authored.graph["links"][0]["from"]["node"] = "lake";
  authored.graph["links"].push_back(
      {{"id", "landform_lake"},
       {"from", {{"node", "landform"}, {"port", "field"}}},
       {"to", {{"node", "lake"}, {"port", "field"}}}});
  authored.graph["links"].push_back(
      {{"id", "lake_surface"},
       {"from", {{"node", "lake"}, {"port", "water"}}},
       {"to", {{"node", "terrain_output"}, {"port", "water"}}}});
  const auto direct = executeTerrainGraph(authored);
  const auto staged = generateTerrainStages(authored);
  assert(direct && staged && staged->waterResult);
  assert(terrainStageRan(*staged, TerrainStage::Hydrology));
  assert(staged->waterResult->surfaces.size() == 1);
  assert(!staged->waterResult->surfaces.front().vertices.empty());
  assert(staged->field->heights == direct->heights);

  std::stop_source cancellation;
  const auto cancelled = executeTerrainGraph(
      authored, {}, cancellation.get_token(), [&](float progress) {
        if (progress > 0.F)
          cancellation.request_stop();
      });
  assert(!cancelled);
}

void exampleLandscapeRetainsBiomesAndWater() {
  const auto sourcePath =
      std::filesystem::path(__FILE__).parent_path().parent_path() /
      "examples/terrain_graph_3d/assets/terrain/"
      "landscape.terrain.json";
  std::ifstream source(sourcePath);
  assert(source && "Terrain graph example source must be available");
  const auto document = nlohmann::json::parse(source);
  const auto authored = TerrainRecipe::parse(document.at("recipe"));
  const auto graph = TerrainGraph::parse(authored.graph);
  const auto executionOrder = graph.executionOrder();
  const auto field = executeTerrainGraph(authored);
  assert(field && field->graphArtifacts);
  assert(field->cellsX == authored.cellsX && field->cellsZ == authored.cellsZ);
  assert(field->heights.size() == authored.sampleCount());
  assert(field->biomeIndices.size() == authored.sampleCount());
  assert(field->graphArtifacts->nodes.size() == executionOrder.size());
  for (const auto &warning : field->graphArtifacts->warnings)
    assert(warning.starts_with("Lake '") &&
           warning.find("not contained") != std::string::npos);

  assert(field->biomeIds.size() == authored.biomes.size());
  for (const auto &id : field->biomeIds)
    assert(authored.biomes.contains(id));
  std::vector<std::size_t> biomeCounts(field->biomeIds.size());
  float lowest = field->heights.front();
  float highest = lowest;
  for (std::size_t sample = 0; sample < field->heights.size(); ++sample) {
    lowest = std::min(lowest, field->heights[sample]);
    highest = std::max(highest, field->heights[sample]);
    ++biomeCounts.at(field->biomeIndices[sample]);
  }
  assert(highest - lowest > 20.F);
  assert(std::count_if(biomeCounts.begin(), biomeCounts.end(),
                       [](std::size_t count) { return count > 0; }) >= 2);

  const auto &artifacts = *field->graphArtifacts;
  std::vector<std::string> waterNodeIds;
  for (const auto &id : executionOrder)
    if (graph.node(id)->type == "water")
      waterNodeIds.push_back(id);
  assert(!waterNodeIds.empty());
  assert(artifacts.water.bodies.size() == waterNodeIds.size());
  for (std::size_t i = 0; i < waterNodeIds.size(); ++i)
    assert(artifacts.water.bodies[i].id == waterNodeIds[i]);
  assert(std::any_of(
      artifacts.water.bodies.begin(), artifacts.water.bodies.end(),
      [](const auto &body) { return body.kind == TerrainWaterBody::Lake; }));
  assert(std::any_of(
      artifacts.water.bodies.begin(), artifacts.water.bodies.end(),
      [](const auto &body) { return body.kind == TerrainWaterBody::River; }));
  assert(artifacts.waterResult && artifacts.waterResult->dropped == 0);
  assert(artifacts.waterResult->surfaces.size() == waterNodeIds.size());
  for (std::size_t i = 0; i < waterNodeIds.size(); ++i)
    assert(artifacts.waterResult->surfaces[i].id == waterNodeIds[i]);
  assert(std::any_of(artifacts.waterResult->surfaces.begin(),
                     artifacts.waterResult->surfaces.end(),
                     [](const auto &surface) {
                       return surface.kind == TerrainWaterBody::River &&
                              !surface.vertices.empty();
                     }));
}

void invalidParameterDomainsAreRejectedAtParse() {
  const auto rejects = [](const nlohmann::json &graph) {
    try {
      (void)TerrainGraph::parse(graph);
      return false;
    } catch (const std::invalid_argument &) {
      return true;
    }
  };
  auto graph = defaultTerrainGraph();
  graph["nodes"].push_back({{"id", "mask"},
                            {"type", "elevation_mask"},
                            {"parameters", {{"falloff", -0.1}}}});
  assert(rejects(graph));

  graph = defaultTerrainGraph();
  graph["nodes"].push_back({{"id", "blend"},
                            {"type", "height_blend"},
                            {"parameters", {{"amount", -0.01}}}});
  assert(rejects(graph));
  graph["nodes"].back()["parameters"]["amount"] = 1.01;
  assert(rejects(graph));

  graph = defaultTerrainGraph();
  graph["nodes"].push_back({{"id", "noise"},
                            {"type", "noise"},
                            {"parameters", {{"feature_size", 0.0}}}});
  assert(rejects(graph));
  graph["nodes"].back()["parameters"] = {{"height_variation", -1.0}};
  assert(rejects(graph));
  graph["nodes"].back()["parameters"] = {{"roughness", 1.01}};
  assert(rejects(graph));

  graph = defaultTerrainGraph();
  graph["nodes"].push_back({{"id", "slope"}, {"type", "slope_mask"}});
  graph["links"].push_back(
      {{"id", "wrong_kind"},
       {"from", {{"node", "slope"}, {"port", "mask"}}},
       {"to", {{"node", "terrain_output"}, {"port", "field"}}}});
  assert(rejects(graph));

  graph = defaultTerrainGraph();
  graph["nodes"].push_back({{"id", "loop"}, {"type", "offset"}});
  graph["links"][0]["from"]["node"] = "loop";
  graph["links"].push_back(
      {{"id", "back_edge"},
       {"from", {{"node", "terrain_output"}, {"port", "field"}}},
       {"to", {{"node", "loop"}, {"port", "field"}}}});
  assert(rejects(graph));
}
} // namespace

void graphWaterAppearanceValidation() {
  const auto rejects = [](const nlohmann::json &graph) {
    try {
      (void)TerrainGraph::parse(graph);
      return false;
    } catch (const std::invalid_argument &) {
      return true;
    }
  };
  auto graph = defaultTerrainGraph();
  graph["nodes"].push_back({{"id", "lake"},
                            {"type", "water"},
                            {"parameters",
                             {{"shallow_color", {0.2, 0.5, 0.6, 0.1}},
                              {"deep_color", {0.0, 0.1, 0.3, 0.9}},
                              {"absorption_distance", 2.0},
                              {"roughness", 0.4}}}});
  assert(!rejects(graph));
  graph["nodes"].back()["parameters"]["shallow_color"] = {2, 0, 0, 1};
  assert(rejects(graph));
  graph["nodes"].back()["parameters"]["shallow_color"] = {0, 0, 0, 1};
  graph["nodes"].back()["parameters"]["absorption_distance"] = 0;
  assert(rejects(graph));
  graph["nodes"].back()["parameters"] = {{"flow_speed", 2},
                                         {"flow_direction", {0, 1}},
                                         {"wave_amplitude", 0.1},
                                         {"wave_length", 3},
                                         {"wave_speed", 0.7},
                                         {"normal_strength", 0.8},
                                         {"foam_width", 0.25},
                                         {"foam_strength", 0.6},
                                         {"foam_color", {1, 1, 1, 0.8}}};
  assert(!rejects(graph));
  for (const auto &invalid : std::vector<nlohmann::json>{
           {{"flow_speed", -1}},
           {{"flow_speed", 1}, {"flow_direction", {0, 0}}},
           {{"flow_direction", {1, 2, 3}}},
           {{"wave_amplitude", -0.1}},
           {{"wave_length", 0}},
           {{"wave_speed", -1}},
           {{"normal_strength", -1}},
           {{"foam_width", -1}},
           {{"foam_strength", 1.1}},
           {{"foam_color", {2, 1, 1, 1}}}}) {
    graph["nodes"].back()["parameters"] = invalid;
    assert(rejects(graph));
  }
}

void commentNodesAreAuthoringOnly() {
  const auto base = TerrainGraph::parse(defaultTerrainGraph());
  auto annotated = base.toJson();
  annotated["nodes"].push_back(
      {{"id", "intro"},
       {"type", "comment"},
       {"parameters",
        {{"text", "Noise shapes the ground.\nOutput publishes it."}}},
       {"position", {10, -200}}});
  const auto parsed = TerrainGraph::parse(annotated);
  assert(parsed.toJson() == annotated);
  assert(parsed.contentKey() == base.contentKey());
  assert(parsed.executionOrder() == base.executionOrder());
  annotated["nodes"].back()["parameters"]["text"] = "A revised explanation";
  assert(TerrainGraph::parse(annotated).contentKey() == base.contentKey());
  annotated["output"] = "intro";
  bool rejected = false;
  try {
    (void)TerrainGraph::parse(annotated);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
}

void pathDistanceMaskPreservesCorridorAndEndpoints() {
  using namespace demi::runtime::terrain_graph_detail;
  auto field = executeTerrainGraph(recipe());
  assert(field);
  const auto source = retained(std::move(*field));
  const nlohmann::json parameters = {
      {"points", {{4, 4}, {4, 4}, {12, 4}, {12, 12}}},
      {"width", 1},
      {"falloff", 2}};
  const auto result = evaluateTerrainPathMask(parameters, source, {});
  assert(result);
  const auto mask =
      std::get<std::shared_ptr<const GraphMask>>(result->outputs.at("mask"));
  assert(mask->source == source);
  assert(mask->samples[source->index(8, 4)] == 1.0F);
  assert(mask->samples[source->index(8, 5)] == 1.0F);
  assert(mask->samples[source->index(8, 6)] == 0.5F);
  assert(mask->samples[source->index(8, 7)] == 0.0F);
  assert(mask->samples[source->index(12, 8)] == 1.0F);
  assert(mask->samples[source->index(2, 4)] == 0.5F);
  assert(mask->samples[source->index(1, 4)] == 0.0F);
  assert(source->height(8, 4) == 3.0F);

  auto hardEdge = parameters;
  hardEdge["falloff"] = 0;
  const auto hardResult = evaluateTerrainPathMask(hardEdge, source, {});
  const auto hardMask = std::get<std::shared_ptr<const GraphMask>>(
      hardResult->outputs.at("mask"));
  assert(hardMask->samples[source->index(8, 5)] == 1.0F);
  assert(hardMask->samples[source->index(8, 6)] == 0.0F);

  std::stop_source cancelled;
  cancelled.request_stop();
  assert(!evaluateTerrainPathMask(parameters, source, cancelled.get_token()));
  auto oversized = parameters;
  oversized["points"][0][0] = 1.0e300;
  bool rejected = false;
  try {
    (void)evaluateTerrainPathMask(oversized, source, {});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
}

void pathDistanceMaskBlendsValleyAndCachesIndependently() {
  auto settings = recipe();
  settings.graph = {
      {"format_version", 1},
      {"nodes",
       {{{"id", "mountains"},
         {"type", "constant"},
         {"parameters", {{"height", 100}}}},
        {{"id", "floor"},
         {"type", "constant"},
         {"parameters", {{"height", 10}}}},
        {{"id", "path"},
         {"type", "path_distance_mask"},
         {"parameters",
          {{"points", {{0, 8}, {16, 8}}}, {"width", 2}, {"falloff", 4}}}},
        {{"id", "valley"},
         {"type", "height_blend"},
         {"parameters", {{"amount", 1}}}},
        {{"id", "output"}, {"type", "output"}}}},
      {"links",
       {{{"id", "p"},
         {"from", {{"node", "mountains"}, {"port", "field"}}},
         {"to", {{"node", "path"}, {"port", "field"}}}},
        {{"id", "a"},
         {"from", {{"node", "mountains"}, {"port", "field"}}},
         {"to", {{"node", "valley"}, {"port", "a"}}}},
        {{"id", "b"},
         {"from", {{"node", "floor"}, {"port", "field"}}},
         {"to", {{"node", "valley"}, {"port", "b"}}}},
        {{"id", "m"},
         {"from", {{"node", "path"}, {"port", "mask"}}},
         {"to", {{"node", "valley"}, {"port", "mask"}}}},
        {{"id", "o"},
         {"from", {{"node", "valley"}, {"port", "field"}}},
         {"to", {{"node", "output"}, {"port", "field"}}}}}},
      {"output", "output"}};
  const auto generated = executeTerrainGraph(settings);
  assert(generated);
  assert(generated->height(8, 8) == 10.0F);
  assert(generated->height(8, 12) == 55.0F);
  assert(generated->height(8, 16) == 100.0F);
  auto widened = settings;
  widened.graph["nodes"][2]["parameters"]["width"] = 4;
  const auto updated = executeTerrainGraph(
      widened, {nullptr, {}, generated->graphArtifacts->cache});
  assert(updated);
  assert(wasCached(*updated, "mountains"));
  assert(wasCached(*updated, "floor"));
  assert(!wasCached(*updated, "path"));
  assert(!wasCached(*updated, "valley"));
  assert(updated->height(8, 12) == 10.0F);
  auto oversized = settings.graph;
  oversized["nodes"][2]["parameters"]["points"][0][0] = 1.0e300;
  bool rejected = false;
  try {
    (void)TerrainGraph::parse(oversized);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
}

void basinWaterEditsUseStableIdsAndPreserveTerrain() {
  auto settings = recipe();
  settings.graph = {
      {"format_version", 1},
      {"output", "output"},
      {"nodes",
       {{{"id", "banks"},
         {"type", "constant"},
         {"parameters", {{"height", 3}}}},
        {{"id", "bed"}, {"type", "constant"}, {"parameters", {{"height", -2}}}},
        {{"id", "footprint"},
         {"type", "polygon_mask"},
         {"parameters",
          {{"points", {{4, 4}, {12, 4}, {12, 12}, {4, 12}}}, {"falloff", 0}}}},
        {{"id", "bowl"},
         {"type", "height_blend"},
         {"parameters", {{"amount", 1}}}},
        {{"id", "drainage"}, {"type", "drainage"}},
        {{"id", "lakes"}, {"type", "basin_water"}},
        {{"id", "edits"}, {"type", "water_edit"}},
        {{"id", "output"}, {"type", "output"}}}},
      {"links", nlohmann::json::array()}};
  const auto connect = [&](std::string_view source, std::string_view sourcePort,
                           std::string_view destination,
                           std::string_view destinationPort) {
    settings.graph["links"].push_back(
        {{"id", std::string(source) + "_" + std::string(destination) + "_" +
                    std::string(destinationPort)},
         {"from", {{"node", source}, {"port", sourcePort}}},
         {"to", {{"node", destination}, {"port", destinationPort}}}});
  };
  connect("banks", "field", "footprint", "field");
  connect("banks", "field", "bowl", "a");
  connect("bed", "field", "bowl", "b");
  connect("footprint", "mask", "bowl", "mask");
  connect("bowl", "field", "drainage", "field");
  connect("drainage", "drainage", "lakes", "drainage");
  connect("lakes", "field", "edits", "field");
  connect("lakes", "water", "edits", "water");
  connect("edits", "field", "output", "field");
  connect("edits", "water", "output", "water");

  const auto proposed = executeTerrainGraph(settings);
  assert(proposed && proposed->graphArtifacts);
  const auto &water = proposed->graphArtifacts->water;
  assert(water.bodies.size() == 1);
  const auto bodyId = water.bodies.front().id;
  assert(bodyId == "lakes/basin/4_4");
  assert(water.bodies.front().level == 3.F);
  assert(proposed->height(8, 8) == -2.F);
  assert(proposed->height(0, 0) == 3.F);
  const auto &initialResult = *proposed->graphArtifacts->waterResult;
  assert(initialResult.resolvedCoverage->wet[proposed->index(8, 8)]);

  settings.graph["nodes"][6]["parameters"]["overrides"] =
      nlohmann::json::array({{{"id", bodyId}, {"level", 1}}});
  const auto lowered = executeTerrainGraph(
      settings, {nullptr, {}, proposed->graphArtifacts->cache});
  assert(lowered && lowered->heights == proposed->heights);
  assert(lowered->graphArtifacts->water.bodies.front().id == bodyId);
  assert(lowered->graphArtifacts->water.bodies.front().level == 1.F);
  assert(wasCached(*lowered, "bowl"));
  assert(wasCached(*lowered, "drainage"));
  assert(wasCached(*lowered, "lakes"));
  assert(!wasCached(*lowered, "edits"));

  settings.graph["nodes"][6]["parameters"] = {
      {"excluded_ids", nlohmann::json::array({bodyId})}};
  const auto excluded = executeTerrainGraph(
      settings, {nullptr, {}, lowered->graphArtifacts->cache});
  assert(excluded && excluded->heights == proposed->heights);
  assert(excluded->graphArtifacts->water.authored);
  assert(excluded->graphArtifacts->water.bodies.empty());
  assert(excluded->graphArtifacts->waterResult->surfaces.empty());
  assert(wasCached(*excluded, "lakes") && !wasCached(*excluded, "edits"));

  std::stop_source cancellation;
  cancellation.request_stop();
  assert(!executeTerrainGraph(settings, {}, cancellation.get_token()));
}

int main() {
  basinWaterEditsUseStableIdsAndPreserveTerrain();
  pathDistanceMaskPreservesCorridorAndEndpoints();
  pathDistanceMaskBlendsValleyAndCachesIndependently();
  commentNodesAreAuthoringOnly();
  paintedBiomesKeepGraphHeights();
  cacheIgnoresAppearanceAndRetainsRuleBoundary();
  paletteFingerprintOnlyInvalidatesScatter();
  rewiredSourceInvalidatesOutput();
  executionOrderFollowsOnlyActiveOutput();
  branchesAndPipelineUseGraphOutput();
  graphStageReportingFollowsNodes();
  graphWaterResultSurvivesPipeline();
  biomeRulesUseConnectedWaterInsteadOfInferredSea();
  emptyConnectedWaterRemainsAuthoritativeForRulesAndScatter();
  exampleLandscapeRetainsBiomesAndWater();
  invalidParameterDomainsAreRejectedAtParse();
  graphWaterAppearanceValidation();
  std::cout << "Terrain graph checks passed\n";
}
