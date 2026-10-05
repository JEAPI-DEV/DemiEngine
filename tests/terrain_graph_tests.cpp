#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainPipeline.h"
#include "demi/runtime/terrain/TerrainUpdate.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <utility>

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
  const auto field = executeTerrainGraph(authored);
  assert(field && field->graphArtifacts);
  assert(field->cellsX == 128 && field->cellsZ == 128);
  assert(field->graphArtifacts->nodes.size() == 13);
  assert(field->graphArtifacts->warnings.empty());

  std::vector<std::size_t> biomeCounts(field->biomeIds.size());
  float lowest = field->heights.front();
  float highest = lowest;
  for (std::size_t sample = 0; sample < field->heights.size(); ++sample) {
    lowest = std::min(lowest, field->heights[sample]);
    highest = std::max(highest, field->heights[sample]);
    ++biomeCounts.at(field->biomeIndices[sample]);
  }
  assert(highest - lowest > 20.F);
  assert(biomeCounts.size() == 5);
  assert(std::count_if(biomeCounts.begin(), biomeCounts.end(),
                       [](std::size_t count) { return count > 0; }) >= 4);

  const auto &artifacts = *field->graphArtifacts;
  assert(artifacts.water.bodies.size() == 2);
  assert(artifacts.water.bodies[0].id == "basin_lake");
  assert(artifacts.water.bodies[1].id == "outlet_river");
  assert(artifacts.waterResult && artifacts.waterResult->dropped == 0);
  assert(artifacts.waterResult->surfaces.size() == 2);
  assert(!artifacts.waterResult->surfaces[1].vertices.empty());
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

int main() {
  paintedBiomesKeepGraphHeights();
  cacheIgnoresAppearanceAndRetainsRuleBoundary();
  paletteFingerprintOnlyInvalidatesScatter();
  rewiredSourceInvalidatesOutput();
  branchesAndPipelineUseGraphOutput();
  graphStageReportingFollowsNodes();
  graphWaterResultSurvivesPipeline();
  exampleLandscapeRetainsBiomesAndWater();
  invalidParameterDomainsAreRejectedAtParse();
  std::cout << "Terrain graph checks passed\n";
}
