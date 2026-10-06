#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainPreset.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace demi::runtime;

namespace {
void expectInvalid(const nlohmann::json &json) {
  bool failed = false;
  try {
    (void)TerrainRecipe::parse(json);
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);
}

TerrainPreset mountainPreset() {
  TerrainPreset preset;
  preset.id = "asset://terrain/presets/mountain_range";
  preset.name = "Mountain range";
  preset.size = {256, 256};
  preset.cellsX = preset.cellsZ = 64;
  preset.chunkCells = 16;
  preset.seed = 4242;
  preset.defaultBiome = "rock";
  preset.defaultLandform = "rock";
  preset.biomes.clear();
  preset.landforms.clear();
  preset.landforms.emplace("rock", TerrainLandform{.baseHeight = 18,
                                                   .heightVariation = 22,
                                                   .featureSize = 26});
  preset.landforms.emplace("scree", TerrainLandform{.baseHeight = 6,
                                                    .heightVariation = 8,
                                                    .featureSize = 18});
  preset.biomes.emplace("rock",
                        TerrainBiome{.landform = "rock",
                                     .color = {.5F, .47F, .42F, 1},
                                     .textureScale = 2});
  preset.biomes.emplace("scree",
                        TerrainBiome{.landform = "scree",
                                     .color = {.6F, .58F, .54F, 1}});
  TerrainBiomeRule peak;
  peak.id = "peak";
  peak.biome = "rock";
  peak.priority = 20;
  peak.elevation = {true, 24, 1e6F};
  preset.rules.push_back(peak);
  TerrainBiomeRule talus;
  talus.id = "talus";
  talus.biome = "scree";
  talus.priority = 10;
  talus.elevation = {true, -1e6F, 24};
  preset.rules.push_back(talus);
  return preset;
}

// A preset supplies generation keys and nothing else. Strokes are authorial.
void fragmentSuppliesOnlyGenerationKeys() {
  const auto fragment = terrainPresetFragment(mountainPreset());
  for (const auto key : terrainPresetGenerationKeys)
    assert(key == "graph" || fragment.contains(key));
  // The fragment carries the preset's shapes and nothing else: a leftover
  // default landform would be an unreferenced entry the author never wrote.
  assert(fragment.at("landforms").size() == mountainPreset().landforms.size());
  for (const auto key : {"regions", "edits", "exclusions"})
    assert(!fragment.contains(key));
  // No provenance until it is applied.
  assert(!fragment.contains("preset_id"));
  assert(!fragment.contains("preset_version"));
  // The fragment is itself a valid recipe, so a preset cannot express
  // something an authored recipe cannot.
  (void)TerrainRecipe::parse(fragment);
  assert(fragment.at("seed") == 4242);
  assert(fragment.at("rules").size() == 2);
  assert(fragment.at("biomes").at("rock").at("texture_scale") == 2);
}

// Applying must never destroy the author's own painting or sculpting.
void applyingPreservesStrokes() {
  nlohmann::json authored = nlohmann::json::object();
  const nlohmann::json region{
      {"biome", "rock"}, {"center", {10, 10}}, {"radius", 5}};
  const nlohmann::json edit{{"type", "flatten"},
                            {"center", {20, 20}},
                            {"radius", 4},
                            {"target_height", 3}};
  const nlohmann::json exclusion{
      {"center", {30, 30}}, {"radius", 4}, {"value", 0.5}};
  authored["regions"] = nlohmann::json::array({region});
  authored["edits"] = nlohmann::json::array({edit});
  authored["exclusions"] = nlohmann::json::array({exclusion});

  const auto merged = applyTerrainPreset(authored, mountainPreset());
  // Generation comes from the preset.
  assert(merged.at("seed") == 4242);
  assert(merged.at("default_biome") == "rock");
  // Strokes survive verbatim.
  assert(merged.at("regions") == authored.at("regions"));
  assert(merged.at("edits") == authored.at("edits"));
  assert(merged.at("exclusions") == authored.at("exclusions"));
  // Provenance is recorded, so the recipe is self-describing.
  assert(merged.at("preset_id") == "asset://terrain/presets/mountain_range");
  assert(merged.at("preset_version") == 1);
  const auto recipe = TerrainRecipe::parse(merged);
  assert(recipe.presetId == "asset://terrain/presets/mountain_range");
  assert(recipe.presetVersion == 1);
}

// A recipe with no strokes must gain only generation keys, never empty arrays
// that would then be written into the author's source.
void applyingToBareRecipeOmitsEmptyStrokes() {
  const auto merged = applyTerrainPreset(nlohmann::json::object(), mountainPreset());
  for (const auto key : {"regions", "edits", "exclusions"})
    assert(!merged.contains(key));
  (void)TerrainRecipe::parse(merged);
}

// Provenance is either absent or complete. A half-stamped recipe would look
// applied when it is not.
void provenanceIsConsistent() {
  // An unstamped recipe is the normal case and must stay valid.
  (void)TerrainRecipe::parse(TerrainRecipe::defaults());

  nlohmann::json stamped = TerrainRecipe::defaults();
  stamped["preset_id"] = "asset://terrain/presets/mountain_range";
  expectInvalid(stamped);

  nlohmann::json versionOnly = TerrainRecipe::defaults();
  versionOnly["preset_version"] = 1;
  expectInvalid(versionOnly);

  nlohmann::json empty = TerrainRecipe::defaults();
  empty["preset_id"] = "";
  empty["preset_version"] = 1;
  expectInvalid(empty);

  nlohmann::json complete = TerrainRecipe::defaults();
  complete["preset_id"] = "asset://terrain/presets/mountain_range";
  complete["preset_version"] = 1;
  const auto parsed = TerrainRecipe::parse(complete);
  assert(parsed.presetId == "asset://terrain/presets/mountain_range");
  // A stamped recipe round-trips, so provenance survives a save. An unstamped
  // one must not invent the keys.
  assert(parsed.toJson() == complete);
  assert(!TerrainRecipe::defaults().contains("preset_id"));
  assert(!TerrainRecipe::defaults().contains("preset_version"));
}

// Provenance is part of the recipe's generation identity: applying a different
// preset is a real recipe change and must not replay as if nothing moved.
void provenanceIsAGenerationInput() {
  const auto before = TerrainRecipe::parse(applyTerrainPreset(
      nlohmann::json::object(), mountainPreset()));
  assert(before.sameGenerationInputs(before));
  auto after = before;
  after.presetId = "asset://terrain/presets/coastal";
  assert(!before.sameGenerationInputs(after));
  after = before;
  after.presetVersion = 2;
  assert(!before.sameGenerationInputs(after));
}

// A preset must produce terrain the shared generator can actually run. This is
// the "presets configure the shared generator, not a second algorithm" check.
void appliedPresetGeneratesTerrain() {
  const auto merged = applyTerrainPreset(nlohmann::json::object(), mountainPreset());
  const auto recipe = TerrainRecipe::parse(merged);
  const auto field = TerrainGenerator::generate(recipe);
  assert(field);
  assert(field->cellsX == 64 && field->cellsZ == 64);
  assert(field->biomeIds.size() == 2);
  bool rock = false, scree = false;
  for (int z = 0; z <= recipe.cellsZ; ++z)
    for (int x = 0; x <= recipe.cellsX; ++x) {
      const auto index = field->index(x, z);
      const auto biome = field->biomeIndices[index];
      if (biome == 0) {
        rock = true;
        assert(field->heights[index] >= 24);
      } else if (biome == 1) {
        scree = true;
        assert(field->heights[index] < 24);
      } else {
        assert(false);
      }
    }
  // The preset's own rule assigns the high ground, so the preset is producing a
  // landscape rather than a flat default.
  assert(rock && scree);
}

// Applying the same preset twice must be idempotent, or re-applying would show
// up as an undoable edit for no reason.
void applyingIsIdempotent() {
  const auto preset = mountainPreset();
  const auto once = applyTerrainPreset(nlohmann::json::object(), preset);
  const auto twice = applyTerrainPreset(once, preset);
  assert(once == twice);
}

// Two presets must differ, or the preset id would be meaningless provenance.
void presetsDiffer() {
  auto other = mountainPreset();
  other.id = "asset://terrain/presets/rolling_hills";
  other.seed = 77;
  const auto a = applyTerrainPreset(nlohmann::json::object(), mountainPreset());
  const auto b = applyTerrainPreset(nlohmann::json::object(), other);
  assert(a != b);
  assert(a.at("seed") != b.at("seed"));
  assert(a.at("preset_id") != b.at("preset_id"));
}

// A preset that would produce an invalid recipe must be rejected at the
// boundary, not at generation time.
void invalidPresetIsRejected() {
  auto preset = mountainPreset();
  preset.defaultBiome = "missing";
  bool failed = false;
  try {
    (void)terrainPresetFragment(preset);
    (void)TerrainRecipe::parse(terrainPresetFragment(preset));
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);

  preset = mountainPreset();
  preset.rules.front().biome = "missing";
  failed = false;
  try {
    (void)TerrainRecipe::parse(terrainPresetFragment(preset));
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);

  preset = mountainPreset();
  preset.rules.front().layer = "no_such_layer";
  failed = false;
  try {
    (void)TerrainRecipe::parse(terrainPresetFragment(preset));
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);
}

// A malformed stroke is rejected, and it is rejected through the merged
// document so the stroke is checked against the biomes it will be generated
// with. A region naming a biome that neither the preset nor the author's own
// recipe defines is an error.
void malformedStrokeIsRejected() {
  nlohmann::json authored = nlohmann::json::object();
  const nlohmann::json badRegion{
      {"biome", "nowhere"}, {"center", {10, 10}}, {"radius", 5}};
  authored["regions"] = nlohmann::json::array({badRegion});
  bool failed = false;
  try {
    (void)applyTerrainPreset(authored, mountainPreset());
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);

  // A region naming a biome the preset does supply is accepted, even though the
  // authored document on its own never declared that biome.
  nlohmann::json good = nlohmann::json::object();
  const nlohmann::json goodRegion{
      {"biome", "scree"}, {"center", {10, 10}}, {"radius", 5}};
  good["regions"] = nlohmann::json::array({goodRegion});
  const auto merged = applyTerrainPreset(good, mountainPreset());
  assert(merged.at("regions").size() == 1);
}
} // namespace

void startersNeedNoProjectAssets() {
  assert(builtinTerrainPresets().size() == 4);
  for (const auto &preset : builtinTerrainPresets()) {
    assert(loadTerrainPreset({}, preset.id)->id == preset.id);
    const auto applied = applyTerrainPreset(TerrainRecipe{}.toJson(), preset);
    const auto graph = TerrainGraph::parse(applied.at("graph"));
    assert(graph.node("shape_help")->type == "comment");
    auto recipe = TerrainRecipe::parse(applied);
    recipe.validate();
    const auto generated = TerrainGenerator::generate(recipe);
    assert(generated && generated->cellsX == preset.cellsX);
    auto withoutComments = recipe;
    auto &nodes = withoutComments.graph["nodes"];
    for (auto it = nodes.begin(); it != nodes.end();) {
      if (it->at("type") == "comment")
        it = nodes.erase(it);
      else
        ++it;
    }
    assert(recipe.sameGenerationInputs(withoutComments));
    const auto document = demi::assets::parseDataDocument(nlohmann::json{
        {"format_version", 1},
        {"name", "Custom"},
        {"graph", preset.graph}}.dump());
    assert(document.document);
    const auto imported =
        parseTerrainPreset(*document.document, "asset://custom/preset");
    assert(imported && imported->graph == preset.graph);
    assert(terrainPresetFragment(*imported)["graph"] == preset.graph);
  }
}

int main() {
  startersNeedNoProjectAssets();
  fragmentSuppliesOnlyGenerationKeys();
  applyingPreservesStrokes();
  applyingToBareRecipeOmitsEmptyStrokes();
  provenanceIsConsistent();
  provenanceIsAGenerationInput();
  appliedPresetGeneratesTerrain();
  applyingIsIdempotent();
  presetsDiffer();
  invalidPresetIsRejected();
  malformedStrokeIsRejected();
  std::cout << "Terrain preset checks passed\n";
}
