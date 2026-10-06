#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include <algorithm>
#include <cassert>
#include <cmath>
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

// Serialization validates too, so an invalid rule must be rejected on either
// path. Wrapping toJson() keeps a rule that only fails validation from
// escaping before the parse is attempted.
void expectInvalid(const TerrainRecipe &recipe) {
  bool failed = false;
  try {
    (void)TerrainRecipe::parse(recipe.toJson());
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);
}

TerrainRecipe ruleRecipe() {
  TerrainRecipe recipe;
  recipe.size = {64, 64};
  recipe.cellsX = recipe.cellsZ = 32;
  recipe.landforms.at("default").baseHeight = 6;
  recipe.landforms.at("default").heightVariation = 10;
  recipe.landforms.emplace("rocky",
                            TerrainLandform{.baseHeight = 14,
                                             .heightVariation = 10});
  recipe.biomes.emplace("rock", TerrainBiome{.landform = "rocky"});
  recipe.biomes.emplace("shore", TerrainBiome{});
  return recipe;
}

// A flat recipe: one default landform at a known height, so a test can assert
// exact values rather than a noise-dependent range.
TerrainRecipe flatRecipe() {
  TerrainRecipe recipe;
  recipe.size = {48, 48};
  recipe.cellsX = recipe.cellsZ = 24;
  recipe.landforms.at("default").heightVariation = 0;
  recipe.landforms.at("default").baseHeight = 0;
  return recipe;
}

std::size_t biomeIndex(const TerrainRecipe &recipe, const std::string &id) {
  std::size_t index = 0;
  for (const auto &[key, biome] : recipe.biomes) {
    if (key == id)
      return index;
    ++index;
  }
  return index;
}

std::vector<TerrainRuleContext> contexts(const TerrainRecipe &recipe) {
  const auto field = TerrainGenerator::generate(recipe);
  assert(field);
  std::vector<float> baseHeights(field->baseHeights.size());
  for (std::size_t i = 0; i < baseHeights.size(); ++i)
    baseHeights[i] = field->baseHeights[i];
  TerrainRuleContextBuilder builder(recipe);
  builder.build(recipe, baseHeights, recipe.cellsX, recipe.cellsZ,
                field->size);
  return builder.contexts();
}

// Round trip and normalization: omitted conventions must not be materialized.
void ruleRoundTrip() {
  auto recipe = ruleRecipe();
  TerrainBiomeRule rule;
  rule.id = "summit";
  rule.biome = "rock";
  rule.priority = 10;
  rule.elevation = {true, 8, 40};
  rule.slope = {true, 0, 30};
  rule.substrate = {TerrainSubstrate::Rock, TerrainSubstrate::Soil};
  recipe.rules.push_back(rule);
  const auto encoded = recipe.toJson();
  const auto decoded = TerrainRecipe::parse(encoded);
  assert(decoded.rules.size() == 1);
  assert(decoded.rules.front().id == "summit");
  assert(decoded.rules.front().biome == "rock");
  assert(decoded.rules.front().priority == 10);
  assert(decoded.rules.front().elevation.enabled);
  assert(decoded.rules.front().elevation.minimum == 8);
  assert(decoded.rules.front().elevation.maximum == 40);
  assert(decoded.rules.front().slope.enabled);
  assert(decoded.rules.front().substrate.size() == 2);
  // Conventions are omitted rather than written back out.
  assert(decoded.toJson() == encoded);
  // A rule without a custom layer omits the default.
  assert(!encoded["rules"][0].contains("layer"));
  assert(!encoded["rules"][0].contains("blend"));

  nlohmann::json minimal{
      {"format_version", 1}, {"rules", nlohmann::json::array()}};
  assert(TerrainRecipe::parse(minimal).rules.empty());
}

void ruleValidation() {
  auto recipe = ruleRecipe();
  recipe.rules.push_back({"", "biomes", "rock", 0, 0, {}, {}, {}, {}, {}});
  expectInvalid(recipe);
  recipe = ruleRecipe();
  recipe.rules.push_back({"a", "biomes", "missing", 0, 0, {}, {}, {}, {}, {}});
  expectInvalid(recipe);
  // Unknown layer kind, and an inverted band, are both rejected.
  recipe = ruleRecipe();
  recipe.rules.push_back({"a", "sculpt", "rock", 0, 0, {}, {}, {}, {}, {}});
  expectInvalid(recipe);
  auto inverted = nlohmann::json::array({nlohmann::json::array({9, 2})});
  nlohmann::json rule{{"id", "a"}, {"biome", "rock"}, {"elevation", inverted}};
  nlohmann::json full{{"format_version", 1}, {"rules", nlohmann::json::array({rule})}};
  expectInvalid(full);
  // Unknown substrate name.
  nlohmann::json bad{{"id", "a"},
                     {"biome", "rock"},
                     {"substrate", nlohmann::json::array({"obsidian"})}};
  expectInvalid(nlohmann::json{{"format_version", 1},
                               {"rules", nlohmann::json::array({bad})}});
  // A typo inside a rule must be rejected, not silently ignored: the editor
  // surfaces parse failures verbatim, so a dropped key would look applied.
  nlohmann::json typo{{"id", "a"}, {"biome", "rock"}, {"elevaton", {1, 2}}};
  expectInvalid(nlohmann::json{{"format_version", 1},
                               {"rules", nlohmann::json::array({typo})}});
}

// Color has no equality operator, so tints are compared per channel.
bool sameColor(const Color &left, const Color &right) {
  return left.r == right.r && left.g == right.g && left.b == right.b &&
         left.a == right.a;
}
bool colorsDiffer(const Color &left, const Color &right) {
  return !sameColor(left, right);
}

// The point of separating appearance from shape: restyling a biome must not
// move a single height sample, and reshaping a landform must not recolour
// anything.
void appearanceAndShapeAreIndependent() {
  auto styled = flatRecipe();
  styled.landforms.at("default").baseHeight = 12;
  styled.landforms.at("default").heightVariation = 6;
  styled.biomes.at("default").color = {0.9F, 0.1F, 0.1F, 1};

  auto restyled = styled;
  restyled.biomes.at("default").color = {0.1F, 0.1F, 0.9F, 1};
  const auto a = *TerrainGenerator::generate(styled);
  const auto b = *TerrainGenerator::generate(restyled);
  // Restyling changes the surface and not one height sample.
  assert(a.heights == b.heights);
  assert(a.baseHeights == b.baseHeights);
  assert(a.biomeColors.size() == 1);
  assert(colorsDiffer(a.biomeColors[0], b.biomeColors[0]));

  auto reshaped = styled;
  reshaped.landforms.at("default").baseHeight = 30;
  const auto c = *TerrainGenerator::generate(reshaped);
  // Reshaping moves the ground and not one tint.
  assert(c.heights != a.heights);
  assert(c.baseHeights != a.baseHeights);
  assert(sameColor(a.biomeColors[0], c.biomeColors[0]));
}

// Two biomes sharing a landform must contribute that shape once, not once each.
// Summing per-biome weights would silently warp the blend.
void sharedLandformIsNotDoubleCounted() {
  TerrainRecipe shared = flatRecipe();
  shared.landforms.at("default").baseHeight = 0;
  shared.landforms.at("default").heightVariation = 0;
  shared.landforms.emplace("high", TerrainLandform{.baseHeight = 10,
                                                    .heightVariation = 0});
  // Both biomes point at the same shape, and the recipe's default does too.
  shared.biomes.emplace("a", TerrainBiome{.landform = "high"});
  shared.biomes.emplace("b", TerrainBiome{.landform = "high"});
  shared.biomes.at("default").landform = "high";
  shared.defaultBiome = "default";
  shared.defaultLandform = "high";
  const auto generated = TerrainGenerator::generate(shared);
  assert(generated);
  const auto &field = *generated;
  // Weight 1 on the default biome, which uses the shared shape, and the other
  // two contribute nothing. If the fold double counted, this would exceed 10.
  for (int z = 0; z <= shared.cellsZ; ++z)
    for (int x = 0; x <= shared.cellsX; ++x)
      assert(std::fabs(field.height(x, z) - 10.F) < 0.001F);
}

// An authored biome that still carries elevation must be refused with an
// actionable message, not silently flattened.
void legacyBiomeShapeIsRejectedWithGuidance() {
  auto recipe = flatRecipe();
  nlohmann::json json = recipe.toJson();
  json["biomes"]["default"]["base_height"] = 4.0;
  bool failed = false;
  std::string message;
  try {
    (void)TerrainRecipe::parse(json);
  } catch (const std::invalid_argument &error) {
    failed = true;
    message = error.what();
  }
  assert(failed);
  assert(message.find("base_height") != std::string::npos);
  assert(message.find("landform") != std::string::npos);
}

// A recipe with no rules must behave exactly as before the feature existed.
void noRulesIsUnchanged() {
  const auto recipe = ruleRecipe();
  const auto field = TerrainGenerator::generate(recipe);
  assert(field);
  // Everything is the default biome when nothing is painted.
  for (std::size_t i = 0; i < field->biomeIndices.size(); ++i)
    assert(field->biomeIndices[i] == biomeIndex(recipe, recipe.defaultBiome));
}

// The generated field's own height distribution. Rules band on elevation, so
// tests must not hardcode a threshold and depend on Perlin output statistics.
struct HeightRange {
  float low = 0;
  float high = 0;
  std::vector<float> all;
};

HeightRange probeHeights(const TerrainRecipe &recipe) {
  const auto field = TerrainGenerator::generate(recipe);
  assert(field);
  HeightRange range;
  range.all.reserve(field->heights.size());
  for (std::size_t i = 0; i < field->heights.size(); ++i)
    range.all.push_back(field->heights[i]);
  std::sort(range.all.begin(), range.all.end());
  range.low = range.all[range.all.size() / 10];
  range.high = range.all[range.all.size() * 9 / 10];
  assert(range.high > range.low);
  return range;
}

// Elevation banding assigns biomes automatically, with no painted regions.
void rulesAssignAutomatically() {
  const auto range = probeHeights(ruleRecipe());
  const float low = range.low;
  const float high = range.high;

  auto recipe = ruleRecipe();
  TerrainBiomeRule summit;
  summit.id = "high_ground";
  summit.biome = "rock";
  summit.priority = 50;
  summit.elevation = {true, high, 1e6F};
  recipe.rules.push_back(summit);
  TerrainBiomeRule basin;
  basin.id = "shore";
  basin.biome = "shore";
  basin.priority = 50;
  basin.elevation = {true, -1e6F, low};
  recipe.rules.push_back(basin);

  const auto field = TerrainGenerator::generate(recipe);
  assert(field);
  const auto rock = biomeIndex(recipe, "rock");
  const auto shore = biomeIndex(recipe, "shore");
  const auto fallback = biomeIndex(recipe, recipe.defaultBiome);
  std::size_t rocks = 0, shores = 0, defaults = 0;
  for (int z = 0; z <= recipe.cellsZ; ++z)
    for (int x = 0; x <= recipe.cellsX; ++x) {
      const auto index = field->index(x, z);
      const auto biome = field->biomeIndices[index];
      const auto height = field->heights[index];
      if (biome == rock) {
        ++rocks;
        // An assigned biome must actually satisfy its own rule.
        assert(height >= high);
      } else if (biome == shore) {
        ++shores;
        assert(height <= low);
      } else {
        assert(biome == fallback);
        ++defaults;
      }
    }
  assert(rocks > 0 && shores > 0 && defaults > 0);
}

// Higher priority wins where two rules both match.
void priorityResolvesConflict() {
  auto recipe = ruleRecipe();
  TerrainBiomeRule wide;
  wide.id = "wide";
  wide.biome = "rock";
  wide.priority = 1;
  wide.elevation = {true, -60, 60};
  TerrainBiomeRule narrow;
  narrow.id = "narrow";
  narrow.biome = "shore";
  narrow.priority = 99;
  narrow.elevation = {true, -60, 0};
  recipe.rules.push_back(wide);
  recipe.rules.push_back(narrow);

  const auto builder = contexts(recipe);
  const auto decisions = assignTerrainBiomes(
      recipe, builder, recipe.cellsX, recipe.cellsZ, recipe.size);
  const auto shore = biomeIndex(recipe, "shore");
  for (int z = 0; z <= recipe.cellsZ; ++z)
    for (int x = 0; x <= recipe.cellsX; ++x) {
      const auto index = std::size_t(z) * (recipe.cellsX + 1) + x;
      if (builder[index].height <= 0)
        assert(decisions[index].biome == shore);
    }
}

// A painted region must beat a matching rule, so manual override stays
// authoritative over automatic assignment.
void paintedRegionsOverrideRules() {
  auto recipe = ruleRecipe();
  TerrainBiomeRule catchAll;
  catchAll.id = "everything";
  catchAll.biome = "rock";
  catchAll.priority = 1000;
  recipe.rules.push_back(catchAll);
  TerrainRegion region;
  region.biome = "shore";
  region.center = {32, 32};
  region.radius = 64;
  region.strength = 1;
  region.falloff = 0;
  recipe.regions.push_back(region);

  const auto builder = contexts(recipe);
  const auto decisions = assignTerrainBiomes(
      recipe, builder, recipe.cellsX, recipe.cellsZ, recipe.size);
  const auto shore = biomeIndex(recipe, "shore");
  assert(decisions[std::size_t(16) * (recipe.cellsX + 1) + 16].biome == shore);
  assert(decisions[std::size_t(16) * (recipe.cellsX + 1) + 16].overridden);
  assert(decisions[std::size_t(16) * (recipe.cellsX + 1) + 16].ruleId.empty());
}

// Disabling the biome layer must remove its rules, same as its regions.
void disabledLayerRemovesRules() {
  auto recipe = ruleRecipe();
  TerrainBiomeRule rule;
  rule.id = "high";
  rule.biome = "rock";
  rule.elevation = {true, -60, 60};
  recipe.rules.push_back(rule);
  for (auto &layer : recipe.layers)
    if (layer.kind == TerrainLayerKind::Biome)
      layer.enabled = false;
  const auto field = TerrainGenerator::generate(recipe);
  assert(field);
  const auto rock = biomeIndex(recipe, "rock");
  for (std::size_t i = 0; i < field->biomeIndices.size(); ++i)
    assert(field->biomeIndices[i] != rock);
}

// The explanation must name the rule that assigned a cell, so the editor and
// CLI can justify a choice instead of leaving the author to guess.
void decisionsExplainThemselves() {
  const auto high = probeHeights(ruleRecipe()).high;
  auto recipe = ruleRecipe();
  TerrainBiomeRule rule;
  rule.id = "high_ground";
  rule.biome = "rock";
  rule.elevation = {true, high, 1e6F};
  recipe.rules.push_back(rule);
  const auto builder = contexts(recipe);
  const auto decisions = assignTerrainBiomes(
      recipe, builder, recipe.cellsX, recipe.cellsZ, recipe.size);
  const auto rock = biomeIndex(recipe, "rock");
  std::size_t explained = 0;
  for (std::size_t i = 0; i < decisions.size(); ++i) {
    if (decisions[i].biome == rock) {
      assert(decisions[i].ruleId == "high_ground");
      ++explained;
    } else {
      assert(decisions[i].ruleId.empty());
    }
  }
  assert(explained > 0);
}

// Substrate is derived, so a substrate rule separates rock from soil.
void substrateSeparatesRock() {
  auto recipe = ruleRecipe();
  TerrainBiomeRule rocky;
  rocky.id = "cliffs";
  rocky.biome = "rock";
  rocky.substrate = {TerrainSubstrate::Rock};
  recipe.rules.push_back(rocky);
  const auto builder = contexts(recipe);
  std::size_t rock = 0, soil = 0;
  for (const auto &context : builder) {
    if (context.substrate == TerrainSubstrate::Rock)
      ++rock;
    else
      ++soil;
  }
  assert(rock > 0 && soil > 0);
  const auto decisions = assignTerrainBiomes(
      recipe, builder, recipe.cellsX, recipe.cellsZ, recipe.size);
  for (std::size_t i = 0; i < decisions.size(); ++i)
    if (builder[i].substrate != TerrainSubstrate::Rock)
      assert(decisions[i].ruleId != "cliffs");
}

// Water distance is a real distance field: zero in water, growing inland, and
// large everywhere when the terrain has no water at all.
void waterDistanceIsMeasured() {
  auto recipe = ruleRecipe();
  const auto builder = contexts(recipe);
  const float sea = TerrainRuleContextBuilder::seaLevel(recipe);
  std::size_t waterSamples = 0;
  for (std::size_t i = 0; i < builder.size(); ++i) {
    if (builder[i].height <= sea) {
      assert(builder[i].waterDistance == 0.F);
      assert(builder[i].substrate == TerrainSubstrate::Wet);
      ++waterSamples;
    } else {
      assert(builder[i].waterDistance > 0.F);
    }
  }
  assert(waterSamples > 0);
  // The maximum must exceed a single cell, so the transform actually spreads.
  float widest = 0;
  for (const auto &context : builder)
    widest = std::max(widest, context.waterDistance);
  assert(widest > recipe.size.x / float(recipe.cellsX));

  // A dry terrain has no shoreline, so no water-distance band may match.
  auto dry = ruleRecipe();
  dry.biomes.clear();
  dry.biomes.emplace("default", TerrainBiome{});
  // A dry, flat surface well above sea level, so no water-distance band can
  // match anywhere.
  dry.landforms.at("default").baseHeight = 40;
  dry.landforms.at("default").heightVariation = 1;
  dry.defaultBiome = "default";
  TerrainBiomeRule nearWater;
  nearWater.id = "lakeside";
  nearWater.biome = "default";
  nearWater.waterDistance = {true, 0, 3};
  dry.rules.push_back(nearWater);
  const auto dryContexts = contexts(dry);
  const auto dryDecisions = assignTerrainBiomes(
      dry, dryContexts, dry.cellsX, dry.cellsZ, dry.size);
  for (std::size_t i = 0; i < dryDecisions.size(); ++i)
    assert(dryDecisions[i].ruleId != "lakeside");
}

// Rules change biome assignment for the whole field, so they must invalidate a
// local replay rather than patching only the edited region.
void rulesInvalidateLocalReplay() {
  const auto before = ruleRecipe();
  auto after = before;
  after.rules.push_back({"high", "biomes", "rock", 5, 0,
                         {true, 9, 60}, {}, {}, {}, {}});
  assert(!before.sameGenerationInputs(after));

  const auto origin = TerrainGenerator::generate(before);
  const auto shared = std::make_shared<const HeightField>(*origin);
  auto sculpt = before;
  TerrainEdit edit;
  edit.kind = TerrainEditKind::Raise;
  edit.center = {32, 32};
  edit.radius = 4;
  edit.amount = 1;
  sculpt.edits.push_back(edit);
  // Sculpting alone stays local.
  const auto local = updateTerrain(before, sculpt, shared);
  assert(local && !local->invalidation.fullGeneration);
  // Changing a rule rebuilds the base.
  const auto rebuilt = updateTerrain(before, after, shared);
  assert(rebuilt && rebuilt->invalidation.fullGeneration);
}
} // namespace

int main() {
  ruleRoundTrip();
  ruleValidation();
  appearanceAndShapeAreIndependent();
  sharedLandformIsNotDoubleCounted();
  legacyBiomeShapeIsRejectedWithGuidance();
  noRulesIsUnchanged();
  rulesAssignAutomatically();
  priorityResolvesConflict();
  paintedRegionsOverrideRules();
  disabledLayerRemovesRules();
  decisionsExplainThemselves();
  substrateSeparatesRock();
  waterDistanceIsMeasured();
  rulesInvalidateLocalReplay();
  std::cout << "Terrain biome rule checks passed\n";
}
