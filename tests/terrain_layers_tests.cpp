#include "demi/runtime/terrain/TerrainBrushStroke.h"
#include "demi/runtime/terrain/TerrainEvaluation.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace demi::runtime;

namespace {
bool near(float left, float right) { return std::abs(left - right) < .0001F; }

template <class Operation> void expectInvalid(Operation operation) {
  bool failed = false;
  try {
    operation();
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);
}

TerrainRecipe flatRecipe() {
  TerrainRecipe recipe;
  recipe.size = {8, 8};
  recipe.cellsX = 8;
  recipe.cellsZ = 8;
  recipe.landforms.at("default").baseHeight = 2;
  recipe.landforms.at("default").heightVariation = 0;
  return recipe;
}

TerrainLayer &layer(TerrainRecipe &recipe, const std::string &id) {
  const auto found =
      std::find_if(recipe.layers.begin(), recipe.layers.end(),
                   [&](const TerrainLayer &entry) { return entry.id == id; });
  assert(found != recipe.layers.end());
  return *found;
}

TerrainEdit hardEdit(TerrainEditKind kind, const std::string &layerId = {}) {
  TerrainEdit edit;
  edit.kind = kind;
  edit.center = {4, 4};
  edit.radius = 2;
  edit.falloff = 0;
  edit.layer = layerId;
  return edit;
}

void authoredDefaultsAndRoundTrip() {
  const auto defaults = TerrainRecipe::parse({{"format_version", 1}});
  assert(defaults.layers.size() == 5);
  assert(defaults.layers[0].id == "generation");
  assert(defaults.layers[0].kind == TerrainLayerKind::Generation);
  assert(defaults.layers[1].id == "biomes");
  assert(defaults.layers[1].kind == TerrainLayerKind::Biome);
  assert(defaults.layers[2].id == "sculpt");
  assert(defaults.layers[3].id == "protection");
  assert(defaults.layers[4].id == "exclusions");
  assert(defaults.exclusions.empty());
  for (const auto &entry : defaults.layers)
    assert(entry.enabled && !entry.name.empty());

  const auto minimal = TerrainRecipe::parse(
      {{"format_version", 1},
       {"regions", nlohmann::json::array({nlohmann::json::object()})},
       {"edits", nlohmann::json::array({{{"type", "raise"}}})},
       {"exclusions", nlohmann::json::array({nlohmann::json::object()})}});
  assert(minimal.regions.front().layer == "biomes");
  assert(minimal.regions.front().biome == "default");
  assert(minimal.regions.front().radius == 8);
  assert(minimal.edits.front().layer.empty());
  assert(minimal.edits.front().amount == 1);
  assert(minimal.exclusions.front().layer == "exclusions");
  assert(minimal.exclusions.front().value == 1);
  const auto canonical = minimal.toJson();
  assert(canonical.at("format_version") == 1);
  assert(!canonical.at("regions")[0].contains("layer"));
  assert(!canonical.at("edits")[0].contains("layer"));
  assert(!canonical.at("exclusions")[0].contains("layer"));
  assert(TerrainRecipe::parse(canonical).toJson() == canonical);

  auto recipe = flatRecipe();
  recipe.layers.push_back(
      {"detail", "Detail passes", TerrainLayerKind::Sculpt, false});
  recipe.edits.push_back(hardEdit(TerrainEditKind::Raise, "detail"));
  const auto source = TerrainGenerator::generate(recipe);
  assert(source);
  recipe.edits.push_back(createProtectionEdit(*source, {4, 4}, 1));
  TerrainExclusion exclusion;
  exclusion.value = .3F;
  recipe.exclusions.push_back(exclusion);
  const auto encoded = recipe.toJson();
  const auto decoded = TerrainRecipe::parse(encoded);
  assert(decoded.layers.back().name == "Detail passes");
  assert(!decoded.layers.back().enabled);
  assert(decoded.edits.front().layer == "detail");
  assert(decoded.edits.back().layer.empty());
  assert(decoded.toJson() == encoded);

  auto named = flatRecipe().toJson();
  named["layers"].push_back({{"id", "extra"}});
  const auto inferred = TerrainRecipe::parse(named);
  assert(inferred.layers.back().name == "extra");
  assert(inferred.layers.back().kind == TerrainLayerKind::Sculpt);
  assert(inferred.layers.back().enabled);
}

void validateLayerContracts() {
  const auto valid = flatRecipe();
  auto invalid = valid;
  invalid.layers.push_back(invalid.layers.front());
  expectInvalid([&] { invalid.validate(); });
  invalid = valid;
  invalid.layers.front().id.clear();
  expectInvalid([&] { invalid.validate(); });
  invalid = valid;
  invalid.layers.erase(invalid.layers.begin());
  expectInvalid([&] { invalid.validate(); });
  invalid = valid;
  invalid.layers.push_back(
      {"other", "Other", TerrainLayerKind::Generation, false});
  expectInvalid([&] { invalid.validate(); });
  invalid = valid;
  invalid.layers.front().enabled = false;
  invalid.validate();
  invalid.layers.front().kind = static_cast<TerrainLayerKind>(100);
  expectInvalid([&] { invalid.validate(); });

  for (const auto &id : {"missing", "sculpt", "generation", ""}) {
    invalid = valid;
    TerrainRegion region;
    region.layer = id;
    invalid.regions.push_back(region);
    expectInvalid([&] { invalid.validate(); });
  }
  for (const auto &id : {"missing", "biomes", "protection", "exclusions"}) {
    invalid = valid;
    invalid.edits.push_back(hardEdit(TerrainEditKind::Raise, id));
    expectInvalid([&] { invalid.validate(); });
  }
  invalid = valid;
  invalid.layers.erase(invalid.layers.begin() + 2);
  invalid.edits.push_back(hardEdit(TerrainEditKind::Raise));
  expectInvalid([&] { invalid.validate(); });

  const auto source = TerrainGenerator::generate(valid);
  assert(source);
  invalid = valid;
  auto protection = createProtectionEdit(*source, {4, 4}, 1);
  protection.layer = "sculpt";
  invalid.edits.push_back(protection);
  expectInvalid([&] { invalid.validate(); });
  invalid.layers[2].enabled = false;
  expectInvalid([&] { invalid.validate(); });

  for (const auto &id : {"missing", "biomes", "sculpt", ""}) {
    invalid = valid;
    TerrainExclusion exclusion;
    exclusion.layer = id;
    invalid.exclusions.push_back(exclusion);
    expectInvalid([&] { invalid.validate(); });
  }
  for (float value : {-1.F, 1.1F, std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN()}) {
    invalid = valid;
    TerrainExclusion exclusion;
    exclusion.value = value;
    invalid.exclusions.push_back(exclusion);
    expectInvalid([&] { invalid.validate(); });
  }

  auto document = valid.toJson();
  document["layers"][0]["kind"] = "generations";
  expectInvalid([&] { (void)TerrainRecipe::parse(document); });
  document = valid.toJson();
  document["layers"][0]["active"] = true;
  expectInvalid([&] { (void)TerrainRecipe::parse(document); });
  document = valid.toJson();
  document["exclusion"] = nlohmann::json::array();
  expectInvalid([&] { (void)TerrainRecipe::parse(document); });
}

void baseStagesAndBiomeOrder() {
  auto recipe = flatRecipe();
  // Each biome names a landform; the shape itself carries the elevation.
  auto hill = recipe.landforms.at("default");
  hill.baseHeight = 10;
  recipe.landforms.emplace("hill", hill);
  recipe.biomes.emplace("hill", TerrainBiome{.landform = "hill"});
  auto sand = hill;
  sand.baseHeight = 20;
  recipe.landforms.emplace("sand", sand);
  recipe.biomes.emplace("sand", TerrainBiome{.landform = "sand"});
  recipe.layers.push_back(
      {"overrides", "Overrides", TerrainLayerKind::Biome, true});
  recipe.regions.push_back({"hill", {4, 4}, 2, 1, 0, "overrides"});
  recipe.regions.push_back({"sand", {4, 4}, 2, 1, 0, "biomes"});
  const TerrainEvaluation ordered(recipe);
  assert(ordered.regions().size() == 2);
  assert(ordered.regions()[0] == &recipe.regions[1]);
  assert(ordered.regions()[1] == &recipe.regions[0]);
  assert(ordered.generationEnabled());
  assert(near(ordered.baseSample({4, 4}).height, 10));
  assert(ordered.baseSample({4, 4}).biome == 1);

  auto changed = recipe;
  layer(changed, "overrides").enabled = false;
  const TerrainEvaluation withoutOverride(changed);
  assert(withoutOverride.regions().size() == 1);
  assert(near(withoutOverride.baseSample({4, 4}).height, 20));
  layer(changed, "biomes").enabled = false;
  assert(near(TerrainEvaluation(changed).baseSample({4, 4}).height, 2));

  changed = recipe;
  layer(changed, "generation").enabled = false;
  const TerrainEvaluation withoutGeneration(changed);
  assert(!withoutGeneration.generationEnabled());
  assert(withoutGeneration.baseSample({4, 4}).height == 0);
  assert(withoutGeneration.baseSample({4, 4}).biome == 1);
  changed.edits.push_back(hardEdit(TerrainEditKind::Raise));
  const auto sculpted = TerrainGenerator::generate(changed);
  assert(sculpted && near(sculpted->height(4, 4), 1));

  recipe.edits.push_back(hardEdit(TerrainEditKind::Raise));
  std::rotate(recipe.layers.begin(), recipe.layers.begin() + 2,
              recipe.layers.end());
  const auto generated = TerrainGenerator::generate(recipe);
  assert(generated && near(generated->height(4, 4), 21));
  assert(generated->biomeIds ==
         (std::vector<std::string>{"default", "hill", "sand"}));
}

void surfaceLayerAndStrokeOrder() {
  auto recipe = flatRecipe();
  recipe.layers.push_back(
      {"details", "Details", TerrainLayerKind::Sculpt, true});
  auto later = hardEdit(TerrainEditKind::Raise, "details");
  later.amount = 3;
  auto flatten = hardEdit(TerrainEditKind::Flatten);
  flatten.targetHeight = 7;
  auto raise = hardEdit(TerrainEditKind::Raise);
  raise.amount = 2;
  recipe.edits = {later, flatten, raise};
  const TerrainEvaluation evaluation(recipe);
  assert(evaluation.edits().size() == 3);
  assert(evaluation.edits()[0] == &recipe.edits[1]);
  assert(evaluation.edits()[1] == &recipe.edits[2]);
  assert(evaluation.edits()[2] == &recipe.edits[0]);
  const auto generated = TerrainGenerator::generate(recipe);
  assert(generated && near(generated->height(4, 4), 12));

  auto disabled = recipe;
  layer(disabled, "sculpt").enabled = false;
  assert(TerrainEvaluation(disabled).edits().size() == 1);
  const auto onlyDetails = TerrainGenerator::generate(disabled);
  assert(onlyDetails && near(onlyDetails->height(4, 4), 5));

  const auto capture = TerrainGenerator::generate(flatRecipe());
  assert(capture);
  const auto protection = createProtectionEdit(*capture, {4, 4}, .5F);
  recipe.edits.insert(recipe.edits.begin(), protection);
  const auto protectedField = TerrainGenerator::generate(recipe);
  assert(protectedField && near(protectedField->height(4, 4), 2));
  assert(near(protectedField->height(5, 4), 12));
  const TerrainEvaluation protectedEvaluation(recipe);
  assert(protectedEvaluation.edits()[2]->kind == TerrainEditKind::Protect);

  disabled = recipe;
  layer(disabled, "protection").enabled = false;
  const auto unprotected = TerrainGenerator::generate(disabled);
  assert(unprotected && near(unprotected->height(4, 4), 12));
}

void exclusionPainting() {
  auto recipe = flatRecipe();
  recipe.layers.push_back(
      {"erase", "Erase exclusions", TerrainLayerKind::Exclusion, true});
  TerrainExclusion erase;
  erase.center = {4, 4};
  erase.radius = 4;
  erase.strength = .25F;
  erase.falloff = 0;
  erase.value = 0;
  erase.layer = "erase";
  TerrainExclusion paint;
  paint.center = {4, 4};
  paint.radius = 2;
  recipe.exclusions = {erase, paint};
  const TerrainEvaluation evaluation(recipe);
  assert(evaluation.exclusions().size() == 2);
  assert(evaluation.exclusions()[0] == &recipe.exclusions[1]);
  assert(evaluation.exclusions()[1] == &recipe.exclusions[0]);
  assert(near(evaluation.exclusion({4, 4}), .75F));
  assert(near(evaluation.exclusion({5, 4}), .375F));
  assert(evaluation.exclusion({6, 4}) == 0);
  assert(evaluation.exclusion({8, 8}) == 0);
  const auto generated = TerrainGenerator::generate(recipe);
  assert(generated && generated->exclusions.size() == recipe.sampleCount());
  assert(near(generated->exclusions[generated->index(4, 4)], .75F));
  assert(near(generated->height(4, 4), 2));

  auto disabled = recipe;
  layer(disabled, "erase").enabled = false;
  assert(near(TerrainEvaluation(disabled).exclusion({4, 4}), 1));
  layer(disabled, "exclusions").enabled = false;
  assert(TerrainEvaluation(disabled).exclusions().empty());
  assert(TerrainEvaluation(disabled).exclusion({4, 4}) == 0);
  const auto empty = TerrainGenerator::generate(disabled);
  assert(empty && empty->heights == generated->heights);
  assert(empty->biomeIndices == generated->biomeIndices);
}

void sharedEvaluationAndSmoothing() {
  auto recipe = flatRecipe();
  recipe.landforms.at("default").heightVariation = 4;
  for (int index = 0; index < 20; ++index)
    recipe.biomes.emplace("biome_" + std::to_string(index), TerrainBiome{});
  recipe.regions.push_back({"biome_9", {4, 4}, 4, .5F, 1});
  const TerrainEvaluation evaluation(recipe);
  const auto generated = TerrainGenerator::generate(recipe);
  assert(generated);
  for (int z = 0; z <= recipe.cellsZ; ++z) {
    for (int x = 0; x <= recipe.cellsX; ++x) {
      const auto sample = evaluation.baseSample(generated->position(x, z));
      const auto index = generated->index(x, z);
      assert(sample.height == generated->baseHeights[index]);
      assert(sample.biome == generated->biomeIndices[index]);
    }
  }

  auto smooth = hardEdit(TerrainEditKind::Smooth);
  smooth.center = {0, 0};
  smooth.radius = 20;
  smooth.strength = .5F;
  const auto read = [&](int x, int z) { return generated->height(x, z); };
  const float corner =
      applyTerrainEdit(smooth, {0, 0}, generated->height(0, 0), read, 0, 0,
                       recipe.cellsX, recipe.cellsZ);
  const float average = (read(0, 0) + read(1, 0) + read(0, 1) + read(1, 1)) / 4;
  assert(near(corner, (read(0, 0) + average) / 2));
  recipe.edits.push_back(smooth);
  const auto smoothed = TerrainGenerator::generate(recipe);
  assert(smoothed);
  for (int z = 0; z <= recipe.cellsZ; ++z) {
    for (int x = 0; x <= recipe.cellsX; ++x) {
      const float expected =
          applyTerrainEdit(smooth, generated->position(x, z), read(x, z), read,
                           x, z, recipe.cellsX, recipe.cellsZ);
      assert(smoothed->height(x, z) == expected);
    }
  }
}

void sharedEditSemantics() {
  const std::function<float(int, int)> unusedSample = [](int, int) -> float {
    throw std::logic_error("A point edit must not read neighbours");
  };
  auto edit = hardEdit(TerrainEditKind::Raise);
  edit.strength = .5F;
  edit.amount = 6;
  assert(near(applyTerrainEdit(edit, {4, 4}, 2, unusedSample, 4, 4, 8, 8), 5));
  edit.kind = TerrainEditKind::Lower;
  assert(near(applyTerrainEdit(edit, {4, 4}, 2, unusedSample, 4, 4, 8, 8), -1));
  edit.kind = TerrainEditKind::Flatten;
  edit.targetHeight = 10;
  assert(near(applyTerrainEdit(edit, {4, 4}, 2, unusedSample, 4, 4, 8, 8), 6));
  assert(near(applyTerrainEdit(edit, {8, 8}, 2, unusedSample, 8, 8, 8, 8), 2));
  edit.kind = TerrainEditKind::Protect;
  assert(near(applyTerrainEdit(edit, {4, 4}, 2, unusedSample, 4, 4, 8, 8), 2));

  edit.kind = TerrainEditKind::Raise;
  edit.strength = 1;
  edit.amount = std::numeric_limits<float>::max();
  expectInvalid([&] {
    (void)applyTerrainEdit(edit, {4, 4}, std::numeric_limits<float>::max(),
                           unusedSample, 4, 4, 8, 8);
  });
}

void protectionBoundsMatchReference() {
  auto recipe = flatRecipe();
  recipe.size = {7.3F, 5.7F};
  recipe.cellsX = 31;
  recipe.cellsZ = 23;
  const auto generated = TerrainGenerator::generate(recipe);
  assert(generated);
  assert(createProtectionEdit(*generated, {3, 3}, 20, 0).samples.empty());
  for (const Vec2 center : {Vec2{0, 0}, Vec2{3.11F, 2.73F}, recipe.size,
                            Vec2{-1, 1}, Vec2{20, 20}}) {
    for (const float radius : {.01F, .7F, 2.F, 20.F}) {
      for (const float falloff : {0.F, 1.F}) {
        const auto edit =
            createProtectionEdit(*generated, center, radius, 1, falloff);
        std::size_t captured = 0;
        for (int z = 0; z <= recipe.cellsZ; ++z) {
          for (int x = 0; x <= recipe.cellsX; ++x) {
            const Vec2 position = generated->position(x, z);
            if (terrainBrushWeight(position, center, radius, 1, falloff) == 0)
              continue;
            assert(captured < edit.samples.size());
            const auto &sample = edit.samples[captured++];
            assert(sample.position.x == position.x);
            assert(sample.position.y == position.y);
            assert(sample.height == generated->height(x, z));
          }
        }
        assert(captured == edit.samples.size());
      }
    }
  }
}

void groupedBrushCodec() {
  using Json = nlohmann::json;
  const auto first = Json::parse(
      R"({"type":"raise","radius":2.25,"strength":0.125,"falloff":1.5,"amount":0.75,"center":[1.125,2.25]})");
  auto second = first;
  second["center"] = Json::array({2.5, 3.75});
  auto later = first;
  later["center"] = Json::array({4.5, 5.75});
  auto interruption = first;
  interruption["strength"] = .25;
  interruption["center"] = Json::array({3.5, 4.75});
  Json stamps = Json::array({first, second, interruption, later});
  const auto compacted = compactTerrainBrushEntries(stamps);
  assert(compacted.size() == 3);
  assert(compacted[0].at("points") ==
         Json::array({first.at("center"), second.at("center")}));
  assert(compacted[0].at("strength") == first.at("strength"));
  assert(compacted[1].at("center") == interruption.at("center"));
  assert(compacted[2].at("center") == later.at("center"));
  assert(expandTerrainBrushEntries(compacted) == stamps);
  assert(compactTerrainBrushEntries(compacted) == compacted);
  Json appended = Json::array();
  for (const auto &stamp : stamps)
    appendTerrainBrushStamp(appended, stamp);
  assert(appended == compacted);
  Json authored{{"format_version", 1},
                {"graph", {{"custom_metadata", Json::array({1, 2, 3})}}},
                {"regions", Json::array()},
                {"edits", stamps},
                {"exclusions", Json::array()}};
  const auto compactRecipe = compactTerrainBrushRecipe(authored);
  assert(compactRecipe.at("edits") == compacted);
  assert(compactRecipe.at("graph") == authored.at("graph"));
  assert(compactRecipe.at("regions") == authored.at("regions"));
  assert(compactRecipe.at("exclusions") == authored.at("exclusions"));
  assert(compactTerrainBrushRecipe(compactRecipe) == compactRecipe);
  expectInvalid([&] { (void)compactTerrainBrushRecipe(Json::array()); });
  auto changedStrength = second;
  changedStrength["strength"] = std::nextafter(.125, 1.0);
  assert(compactTerrainBrushEntries(Json::array({first, changedStrength}))
             .size() == 2);

  // A center remains a valid single radial operation, including the existing
  // omitted-center default. Snapshots cannot be shared across points.
  assert(compactTerrainBrushEntries(Json::array({first}))[0] == first);
  assert(expandTerrainBrushEntries(Json::array({Json::object()})) ==
         Json::array({Json::object()}));
  auto snapshot = first;
  snapshot["snapshot"] = Json::object();
  assert(compactTerrainBrushEntries(Json::array({snapshot, snapshot})).size() ==
         2);
  expectInvalid([&] {
    Json entries = Json::array();
    appendTerrainBrushStamp(entries, Json{{"points", Json::array({{1, 2}})}});
  });
  for (const auto &bad :
       {Json{{"center", {1, 2}}, {"points", Json::array({{1, 2}})}},
        Json{{"points", Json::array()}},
        Json{{"points", Json::array({Json::array({1})})}},
        Json{{"points", Json::array({Json::array({1, "z"})})}},
        Json{{"points", Json::array({Json::array({1e100, 2})})}},
        Json{{"type", "protect"},
             {"points", Json::array({Json::array({1, 2})})}},
        Json{{"points", Json::array({Json::array({1, 2})})},
             {"snapshot", Json::object()}}}) {
    expectInvalid([&] { (void)expandTerrainBrushEntries(Json::array({bad})); });
  }
}

void groupedRecipeReplay() {
  auto original = flatRecipe();
  original.biomes.emplace("hill", TerrainBiome{});
  original.regions = {{"hill", {2, 2}, 2, .75F, 1, "biomes"},
                      {"hill", {3, 2}, 2, .75F, 1, "biomes"},
                      {"default", {4, 2}, 1, 1, 0, "biomes"}};
  auto raise = hardEdit(TerrainEditKind::Raise);
  raise.center = {2, 2};
  raise.strength = .375F;
  raise.amount = .5F;
  auto second = raise;
  second.center = {3, 2};
  auto flatten = hardEdit(TerrainEditKind::Flatten);
  flatten.targetHeight = 4;
  original.edits = {raise, second, flatten, raise};
  TerrainExclusion exclusion;
  exclusion.center = {2, 2};
  exclusion.value = .25F;
  original.exclusions.push_back(exclusion);
  exclusion.center = {3, 2};
  original.exclusions.push_back(exclusion);

  const auto encoded = original.toJson();
  assert(encoded.at("regions").size() == 2);
  assert(encoded.at("regions")[0].at("points").size() == 2);
  assert(encoded.at("edits").size() == 3);
  assert(encoded.at("edits")[0].at("points").size() == 2);
  assert(encoded.at("edits")[2].contains("center"));
  assert(encoded.at("exclusions").size() == 1);
  assert(encoded.at("exclusions")[0].at("points").size() == 2);
  const auto decoded = TerrainRecipe::parse(encoded);
  assert(decoded.regions.size() == original.regions.size());
  assert(decoded.edits.size() == original.edits.size());
  assert(decoded.exclusions.size() == original.exclusions.size());
  assert(decoded.toJson() == encoded);
  const auto before = TerrainGenerator::generate(original);
  const auto after = TerrainGenerator::generate(decoded);
  assert(before && after);
  assert(before->heights == after->heights);
  assert(before->biomeIndices == after->biomeIndices);
  assert(before->exclusions == after->exclusions);

  auto protectedRecipe = flatRecipe();
  const auto source = TerrainGenerator::generate(protectedRecipe);
  assert(source);
  const auto protection = createProtectionEdit(*source, {4, 4}, 2);
  protectedRecipe.edits = {protection, protection};
  const auto protectedJson = protectedRecipe.toJson();
  assert(protectedJson.at("edits").size() == 2);
  assert(protectedJson.at("edits")[0].contains("snapshot"));
  assert(protectedJson.at("edits")[1].contains("center"));
  assert(TerrainRecipe::parse(protectedJson).toJson() == protectedJson);

  auto bad = encoded;
  bad["edits"][0]["points"] = nlohmann::json::array();
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  bad = encoded;
  bad["regions"][0]["center"] = {1, 2};
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  bad = encoded;
  bad["exclusions"][0]["points"][1] = {1};
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
}
} // namespace

int main() {
  authoredDefaultsAndRoundTrip();
  validateLayerContracts();
  baseStagesAndBiomeOrder();
  surfaceLayerAndStrokeOrder();
  exclusionPainting();
  sharedEvaluationAndSmoothing();
  sharedEditSemantics();
  protectionBoundsMatchReference();
  groupedBrushCodec();
  groupedRecipeReplay();
}
