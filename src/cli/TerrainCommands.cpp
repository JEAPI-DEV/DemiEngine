#include "cli/TerrainCommands.h"
#include "cli/TerrainCompactCommand.h"

#include "cli/CliArguments.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/TerrainAssetValidation.h"
#include "demi/filesystem/AtomicTextFile.h"
#include "demi/filesystem/ProjectDiscovery.h"
#include "demi/runtime/scene/Component.h"
#include "demi/runtime/scene/ProjectParser.h"
#include "demi/runtime/scene/SceneEntityParser.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/composition/EntityHierarchy.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/runtime/scene/model/ProjectData.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainPreset.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "demi/runtime/terrain/TerrainSeed.h"
#include "demi/filesystem/AuthoredJsonPatch.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace demi::cli {
namespace {

using Json = nlohmann::json;

constexpr int ExitValidationFailure = 1;
constexpr int ExitUsageError = 2;

constexpr std::string_view PresetContentType = "terrain_preset";
constexpr std::string_view TerrainComponent = "Terrain3D";

constexpr std::array<runtime::TerrainSeedChannel, 5> SeedChannels{
    runtime::TerrainSeedChannel::Landform,
    runtime::TerrainSeedChannel::Erosion,
    runtime::TerrainSeedChannel::Hydrology,
    runtime::TerrainSeedChannel::BiomePlacement,
    runtime::TerrainSeedChannel::Scatter};

std::string_view layerKindName(runtime::TerrainLayerKind kind) {
  switch (kind) {
  case runtime::TerrainLayerKind::Generation:
    return "generation";
  case runtime::TerrainLayerKind::Biome:
    return "biome";
  case runtime::TerrainLayerKind::Sculpt:
    return "sculpt";
  case runtime::TerrainLayerKind::Protection:
    return "protection";
  case runtime::TerrainLayerKind::Exclusion:
    return "exclusion";
  }
  return "unknown";
}

std::string_view editKindName(runtime::TerrainEditKind kind) {
  switch (kind) {
  case runtime::TerrainEditKind::Raise:
    return "raise";
  case runtime::TerrainEditKind::Lower:
    return "lower";
  case runtime::TerrainEditKind::Flatten:
    return "flatten";
  case runtime::TerrainEditKind::Smooth:
    return "smooth";
  case runtime::TerrainEditKind::Protect:
    return "protect";
  }
  return "unknown";
}

std::size_t countEdits(const runtime::TerrainRecipe &recipe,
                       runtime::TerrainEditKind kind) {
  std::size_t count = 0;
  for (const runtime::TerrainEdit &edit : recipe.edits)
    if (edit.kind == kind)
      ++count;
  return count;
}

// Text reports use a fixed three-decimal form so a band bound and a sample
// value line up on the page and two runs of one recipe print identical bytes.
std::string fixed(float value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(3) << value;
  return stream.str();
}

std::size_t columnWidth(const std::vector<std::string> &values) {
  std::size_t width = 0;
  for (const std::string &value : values)
    width = std::max(width, value.size());
  return width;
}

bool parseInteger(const std::string &text, int &value) {
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parseNumber(const std::string &text, float &value) {
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

std::optional<Json> readJson(const std::string &path, std::ostream &error) {
  std::ifstream input(path);
  if (!input) {
    error << "Failed to read terrain file: " << path << '\n';
    return std::nullopt;
  }
  try {
    return Json::parse(input);
  } catch (const Json::parse_error &e) {
    error << "Invalid JSON in " << path << ": " << e.what() << '\n';
    return std::nullopt;
  }
}

std::optional<runtime::TerrainRecipe> loadRecipe(const std::string &path,
                                                 std::ostream &error) {
  const auto document = readJson(path, error);
  if (!document)
    return std::nullopt;
  try {
    return runtime::TerrainRecipe::parse(*document);
  } catch (const std::invalid_argument &e) {
    error << "Invalid terrain recipe in " << path << ": " << e.what() << '\n';
    // A scene keeps its recipe under the Terrain3D component, so a scene handed
    // to this verb is a likely mistake worth naming rather than leaving the
    // author to compare the message against the recipe schema.
    if (document->is_object() && document->contains("entities"))
      error << "That looks like a scene document. A terrain recipe is the "
               "Terrain3D component's \"recipe\" object, not the scene itself.\n";
    return std::nullopt;
  } catch (const std::exception &e) {
    error << "Failed to load terrain recipe " << path << ": " << e.what()
          << '\n';
    return std::nullopt;
  }
}

Json seedTable(int worldSeed) {
  Json channels = Json::array();
  for (const runtime::TerrainSeedChannel channel : SeedChannels)
    channels.push_back(
        {{"channel", std::string(runtime::terrainSeedChannelName(channel))},
         {"seed", runtime::deriveTerrainSubSeed(worldSeed, channel)}});
  return channels;
}

Json ruleToJson(const runtime::TerrainBiomeRule &rule) {
  Json json{{"id", rule.id},
            {"layer", rule.layer},
            {"biome", rule.biome},
            {"priority", rule.priority},
            {"blend", rule.blend},
            {"elevation", rule.elevation.toJson()},
            {"slope", rule.slope.toJson()},
            {"moisture", rule.moisture.toJson()},
            {"water_distance", rule.waterDistance.toJson()}};
  if (!rule.substrate.empty()) {
    Json names = Json::array();
    for (const runtime::TerrainSubstrate substrate : rule.substrate)
      names.push_back(std::string(runtime::terrainSubstrateName(substrate)));
    json["substrate"] = std::move(names);
  }
  return json;
}

Json inspectToJson(const runtime::TerrainRecipe &recipe) {
  // Landforms are reported separately from biomes, because a biome's appearance
  // and the ground it stands on are authored independently now.
  Json landforms = Json::array();
  for (const auto &[id, shape] : recipe.landforms)
    landforms.push_back(
        {{"id", id},
         {"base_height", shape.baseHeight},
         {"height_variation", shape.heightVariation},
         {"feature_size", shape.featureSize},
         {"roughness", shape.roughness},
         {"octaves", shape.octaves}});

  Json biomes = Json::array();
  for (const auto &[id, biome] : recipe.biomes) {
    Json entry{{"id", id},
               {"landform", biome.landform.empty() ? recipe.defaultLandform
                                                   : biome.landform},
               {"color",
                {biome.color.r, biome.color.g, biome.color.b, biome.color.a}}};
    if (!biome.material.empty())
      entry["material"] = biome.material;
    biomes.push_back(std::move(entry));
  }

  Json layers = Json::array();
  for (const runtime::TerrainLayer &layer : recipe.layers)
    layers.push_back({{"id", layer.id},
                      {"name", layer.name},
                      {"kind", std::string(layerKindName(layer.kind))},
                      {"enabled", layer.enabled}});

  Json rules = Json::array();
  for (const runtime::TerrainBiomeRule &rule : recipe.rules)
    rules.push_back(ruleToJson(rule));

  std::size_t protectionSamples = 0;
  for (const runtime::TerrainEdit &edit : recipe.edits)
    if (edit.kind == runtime::TerrainEditKind::Protect)
      protectionSamples += edit.samples.size();

  return {
      {"format_version", 1},
      {"size", {recipe.size.x, recipe.size.y}},
      {"resolution", {recipe.cellsX, recipe.cellsZ}},
      {"sample_count", recipe.sampleCount()},
      {"seed", recipe.seed},
      {"chunk_cells", recipe.chunkCells},
      {"default_biome", recipe.defaultBiome},
      {"sea_level", runtime::TerrainRuleContextBuilder::seaLevel(recipe)},
      {"default_landform", recipe.defaultLandform},
      {"landforms", std::move(landforms)},
      {"biomes", std::move(biomes)},
      {"layers", std::move(layers)},
      {"rules", std::move(rules)},
      {"strokes",
       {{"regions", recipe.regions.size()},
        {"edits", recipe.edits.size()},
        {"exclusions", recipe.exclusions.size()},
        {"protection_samples", protectionSamples},
        {"edit_kinds",
         {{"raise", countEdits(recipe, runtime::TerrainEditKind::Raise)},
          {"lower", countEdits(recipe, runtime::TerrainEditKind::Lower)},
          {"flatten", countEdits(recipe, runtime::TerrainEditKind::Flatten)},
          {"smooth", countEdits(recipe, runtime::TerrainEditKind::Smooth)},
          {"protect", countEdits(recipe, runtime::TerrainEditKind::Protect)}}}}},
      {"sub_seeds", seedTable(recipe.seed)}};
}

// Condition labels are padded to one width so a rule's bands read as a column.
constexpr int ConditionLabelWidth = 15;

void printBand(std::ostream &out, std::string_view label,
               const runtime::TerrainRuleBand &band) {
  out << "    " << std::setw(ConditionLabelWidth) << std::left << label
      << std::right;
  if (!band.enabled) {
    out << "unconstrained\n";
    return;
  }
  out << '[' << fixed(band.minimum) << ',' << fixed(band.maximum) << "]\n";
}

void printSubstrateRule(std::ostream &out,
                        const runtime::TerrainBiomeRule &rule) {
  out << "    " << std::setw(ConditionLabelWidth) << std::left << "substrate"
      << std::right;
  if (rule.substrate.empty()) {
    out << "unconstrained\n";
    return;
  }
  for (std::size_t i = 0; i < rule.substrate.size(); ++i)
    out << (i == 0 ? "" : ",")
        << runtime::terrainSubstrateName(rule.substrate[i]);
  out << '\n';
}

void printInspectText(const runtime::TerrainRecipe &recipe,
                      const std::string &path, std::ostream &out) {
  out << "Terrain recipe: " << path << '\n'
      << "Size: " << fixed(recipe.size.x) << " x " << fixed(recipe.size.y)
      << '\n'
      << "Resolution: " << recipe.cellsX << " x " << recipe.cellsZ << " ("
      << recipe.sampleCount() << " samples)\n"
      << "Seed: " << recipe.seed << '\n'
      << "Chunk cells: " << recipe.chunkCells << '\n'
      << "Default biome: " << recipe.defaultBiome << '\n'
      << "Sea level: "
      << fixed(runtime::TerrainRuleContextBuilder::seaLevel(recipe)) << '\n';

  std::vector<std::string> ids;
  ids.reserve(recipe.biomes.size());
  for (const auto &[id, biome] : recipe.biomes) {
    (void)biome;
    ids.push_back(id);
  }
  const std::size_t width = columnWidth(ids);
  out << "Landforms (" << recipe.landforms.size() << "), default \""
      << recipe.defaultLandform << "\":\n";
  for (const auto &[id, shape] : recipe.landforms)
    out << "  " << id << std::string(width - id.size() + 2, ' ')
        << "base_height=" << fixed(shape.baseHeight)
        << " height_variation=" << fixed(shape.heightVariation)
        << " feature_size=" << fixed(shape.featureSize)
        << " roughness=" << fixed(shape.roughness)
        << " octaves=" << shape.octaves << '\n';

  out << "Biomes (" << recipe.biomes.size() << "):\n";
  for (const auto &[id, biome] : recipe.biomes)
    out << "  " << id << std::string(width - id.size() + 2, ' ')
        << "landform="
        << (biome.landform.empty() ? recipe.defaultLandform : biome.landform)
        << " color=" << fixed(biome.color.r) << ',' << fixed(biome.color.g)
        << ',' << fixed(biome.color.b) << ',' << fixed(biome.color.a) << '\n';

  out << "Layers (" << recipe.layers.size() << "):\n";
  for (const runtime::TerrainLayer &layer : recipe.layers)
    out << "  " << layer.id << " \"" << layer.name << "\" kind="
        << layerKindName(layer.kind)
        << " enabled=" << (layer.enabled ? "true" : "false") << '\n';

  out << "Rules (" << recipe.rules.size() << "):\n";
  if (recipe.rules.empty())
    out << "  (none: every sample keeps the default biome plus painted "
           "regions)\n";
  for (const runtime::TerrainBiomeRule &rule : recipe.rules) {
    out << "  " << rule.id << " biome=" << rule.biome
        << " priority=" << rule.priority << " layer=" << rule.layer
        << " blend=" << fixed(rule.blend) << '\n';
    printBand(out, "elevation", rule.elevation);
    printBand(out, "slope", rule.slope);
    printBand(out, "moisture", rule.moisture);
    printBand(out, "water distance", rule.waterDistance);
    printSubstrateRule(out, rule);
  }

  std::size_t protectionSamples = 0;
  for (const runtime::TerrainEdit &edit : recipe.edits)
    if (edit.kind == runtime::TerrainEditKind::Protect)
      protectionSamples += edit.samples.size();
  out << "Strokes:\n"
      << "  regions: " << recipe.regions.size() << '\n'
      << "  edits: " << recipe.edits.size() << " (";
  bool first = true;
  for (const auto kind :
       {runtime::TerrainEditKind::Raise, runtime::TerrainEditKind::Lower,
        runtime::TerrainEditKind::Flatten, runtime::TerrainEditKind::Smooth,
        runtime::TerrainEditKind::Protect}) {
    out << (first ? "" : ", ") << editKindName(kind) << '='
        << countEdits(recipe, kind);
    first = false;
  }
  out << ")\n"
      << "  exclusions: " << recipe.exclusions.size() << '\n'
      << "  protection samples: " << protectionSamples << '\n';

  out << "Sub-seeds from world seed " << recipe.seed << ":\n";
  for (const runtime::TerrainSeedChannel channel : SeedChannels)
    out << "  " << runtime::terrainSeedChannelName(channel) << ' '
        << runtime::deriveTerrainSubSeed(recipe.seed, channel) << '\n';
}

struct AtPoint {
  float x = 0;
  float z = 0;
};

std::optional<AtPoint> parseAt(const std::string &text,
                               const runtime::TerrainRecipe &recipe,
                               std::ostream &error) {
  const auto separator = text.find(',');
  if (separator == std::string::npos ||
      text.find(',', separator + 1) != std::string::npos) {
    error << "--at must be two comma-separated numbers: <x>,<z>\n";
    return std::nullopt;
  }
  AtPoint point;
  if (!parseNumber(text.substr(0, separator), point.x) ||
      !parseNumber(text.substr(separator + 1), point.z)) {
    error << "--at must be two comma-separated numbers: <x>,<z>\n";
    return std::nullopt;
  }
  if (!std::isfinite(point.x) || !std::isfinite(point.z)) {
    error << "--at coordinates must be finite numbers: " << text << '\n';
    return std::nullopt;
  }
  if (point.x < 0 || point.x > recipe.size.x || point.z < 0 ||
      point.z > recipe.size.y) {
    error << "--at " << text << " is outside the terrain bounds 0.."
          << fixed(recipe.size.x) << " x 0.." << fixed(recipe.size.y) << '\n';
    return std::nullopt;
  }
  return point;
}

struct CellAddress {
  int x = 0;
  int z = 0;
};

CellAddress locateCell(const runtime::TerrainRecipe &recipe, float x, float z) {
  // A position on the far edge belongs to no cell, so it resolves to the last
  // sample instead of failing: the edge is inside the authored terrain.
  const auto column =
      static_cast<int>(std::floor(double(x) / double(recipe.size.x) *
                                  double(recipe.cellsX)));
  const auto row = static_cast<int>(std::floor(double(z) / double(recipe.size.y) *
                                               double(recipe.cellsZ)));
  return {std::clamp(column, 0, recipe.cellsX),
          std::clamp(row, 0, recipe.cellsZ)};
}

// Mirrors the runtime's active-rule selection: only rules on an enabled biome
// layer are evaluated, ordered by authored layer order then rule order, so the
// listing is the same list the assignment pass considered.
std::vector<const runtime::TerrainBiomeRule *>
activeRules(const runtime::TerrainRecipe &recipe) {
  std::vector<const runtime::TerrainBiomeRule *> active;
  for (const runtime::TerrainLayer &layer : recipe.layers) {
    if (!layer.enabled || layer.kind != runtime::TerrainLayerKind::Biome)
      continue;
    for (const runtime::TerrainBiomeRule &rule : recipe.rules)
      if (rule.layer == layer.id)
        active.push_back(&rule);
  }
  return active;
}

struct ConditionVerdict {
  const char *name;
  bool constrained = false;
  float minimum = 0;
  float maximum = 0;
  float sample = 0;
  bool inside = true;
};

struct RuleVerdict {
  const runtime::TerrainBiomeRule *rule = nullptr;
  std::vector<ConditionVerdict> bands;
  std::vector<std::string> substrate;
  std::string substrateSample;
  bool substrateConstrained = false;
  bool substrateInside = true;
  bool rejected = false;
  std::string rejection;
};

RuleVerdict judge(const runtime::TerrainBiomeRule &rule,
                  const runtime::TerrainRuleContext &context) {
  RuleVerdict verdict;
  verdict.rule = &rule;
  const auto band = [&](const char *name,
                        const runtime::TerrainRuleBand &value, float sample) {
    const bool inside = value.contains(sample);
    verdict.bands.push_back(
        {name, value.enabled, value.minimum, value.maximum, sample, inside});
    if (!inside && !verdict.rejected) {
      verdict.rejected = true;
      verdict.rejection = name;
    }
  };
  // Evaluated in the runtime's order so the first reported rejection is the
  // first condition the assignment pass would have rejected on.
  if (!rule.substrate.empty()) {
    verdict.substrateConstrained = true;
    verdict.substrateSample =
        std::string(runtime::terrainSubstrateName(context.substrate));
    for (const runtime::TerrainSubstrate entry : rule.substrate)
      verdict.substrate.push_back(
          std::string(runtime::terrainSubstrateName(entry)));
    verdict.substrateInside =
        std::find(rule.substrate.begin(), rule.substrate.end(),
                  context.substrate) != rule.substrate.end();
    if (!verdict.substrateInside) {
      verdict.rejected = true;
      verdict.rejection = "substrate";
    }
  }
  band("slope", rule.slope, context.slope);
  band("moisture", rule.moisture, context.moisture);
  band("water distance", rule.waterDistance, context.waterDistance);
  band("elevation", rule.elevation, context.height);
  return verdict;
}

void printConditions(std::ostream &out, const RuleVerdict &verdict) {
  const auto label = [&out](std::string_view name) {
    out << "      " << std::setw(ConditionLabelWidth) << std::left << name
        << std::right;
  };
  for (const ConditionVerdict &band : verdict.bands) {
    if (!band.constrained) {
      label(band.name);
      out << "unconstrained\n";
      continue;
    }
    label(band.name);
    out << '[' << fixed(band.minimum) << ',' << fixed(band.maximum)
        << "] sample " << fixed(band.sample) << " -> "
        << (band.inside ? "INSIDE" : "OUTSIDE") << '\n';
  }
  if (!verdict.substrateConstrained) {
    label("substrate");
    out << "unconstrained\n";
    return;
  }
  label("substrate");
  for (std::size_t i = 0; i < verdict.substrate.size(); ++i)
    out << (i == 0 ? "" : ",") << verdict.substrate[i];
  out << " sample " << verdict.substrateSample << " -> "
      << (verdict.substrateInside ? "INSIDE" : "OUTSIDE") << '\n';
}

Json conditionsToJson(const RuleVerdict &verdict) {
  Json bands = Json::array();
  for (const ConditionVerdict &band : verdict.bands)
    bands.push_back({{"name", band.name},
                     {"constrained", band.constrained},
                     {"minimum", band.minimum},
                     {"maximum", band.maximum},
                     {"sample", band.sample},
                     {"inside", band.inside}});
  Json json{
      {"bands", std::move(bands)},
      {"substrate",
       {{"constrained", verdict.substrateConstrained},
        {"accepted", verdict.substrate},
        {"sample", verdict.substrateConstrained
                       ? Json(verdict.substrateSample)
                       : Json(nullptr)},
        {"inside", verdict.substrateInside}}}};
  json["verdict"] =
      verdict.rejected ? std::string("rejected by ") + verdict.rejection
                       : std::string("every condition contains the sample");
  return json;
}

struct Explanation {
  AtPoint point;
  CellAddress cell;
  std::size_t index = 0;
  float seaLevel = 0;
  runtime::TerrainRuleContext context;
  std::string biome;
  std::string ruleId;
  std::string source;
  bool overridden = false;
  std::vector<RuleVerdict> verdicts;
};

Json explainToJson(const runtime::TerrainRecipe &recipe,
                   const Explanation &explanation) {
  const runtime::TerrainBiomeRule *matched = nullptr;
  for (const RuleVerdict &verdict : explanation.verdicts)
    if (!explanation.ruleId.empty() && verdict.rule->id == explanation.ruleId)
      matched = verdict.rule;

  Json candidates = Json::array();
  for (const RuleVerdict &verdict : explanation.verdicts)
    candidates.push_back(
        {{"id", verdict.rule->id},
         {"biome", verdict.rule->biome},
         {"priority", verdict.rule->priority},
         {"blend", verdict.rule->blend},
         {"selected", matched == verdict.rule},
         {"conditions", conditionsToJson(verdict)}});

  Json json{
      {"format_version", 1},
      {"position", {{"x", explanation.point.x}, {"z", explanation.point.z}}},
      {"terrain",
       {{"width", recipe.size.x},
        {"depth", recipe.size.y},
        {"cells_x", recipe.cellsX},
        {"cells_z", recipe.cellsZ},
        {"sample_count", recipe.sampleCount()}}},
      {"cell",
       {{"x", explanation.cell.x},
        {"z", explanation.cell.z},
        {"index", explanation.index}}},
      {"sea_level", explanation.seaLevel},
      {"context",
       {{"height", explanation.context.height},
        {"slope_degrees", explanation.context.slope},
        {"moisture", explanation.context.moisture},
        {"water_distance", explanation.context.waterDistance},
        {"substrate", std::string(runtime::terrainSubstrateName(
                          explanation.context.substrate))}}},
      {"biome", explanation.biome},
      {"source", explanation.source},
      {"overridden", explanation.overridden},
      {"rule_id", matched == nullptr ? Json(nullptr) : Json(explanation.ruleId)},
      {"rule", matched == nullptr ? Json(nullptr) : ruleToJson(*matched)},
      {"candidates", std::move(candidates)}};
  return json;
}

void printExplainText(const runtime::TerrainRecipe &recipe,
                      const Explanation &explanation, std::ostream &out) {
  out << "Position: " << fixed(explanation.point.x) << ','
      << fixed(explanation.point.z) << " in terrain " << fixed(recipe.size.x)
      << " x " << fixed(recipe.size.y) << '\n'
      << "Cell: " << explanation.cell.x << ',' << explanation.cell.z
      << " (sample " << explanation.index << " of " << recipe.sampleCount()
      << ")\n"
      << "Sea level: " << fixed(explanation.seaLevel) << "\n\n"
      << "Biome: " << explanation.biome << '\n';

  if (explanation.overridden) {
    out << "Source: painted region override\n"
        << "A painted region always wins over rules and the default biome.\n";
  } else if (explanation.source == "rule") {
    out << "Source: rule \"" << explanation.ruleId << "\"\n";
  } else {
    out << "Source: default biome (no rule matched this sample)\n";
  }

  out << "\nContext:\n"
      << "  height          " << fixed(explanation.context.height) << '\n'
      << "  slope           " << fixed(explanation.context.slope) << " deg\n"
      << "  moisture        " << fixed(explanation.context.moisture) << '\n'
      << "  water distance  " << fixed(explanation.context.waterDistance)
      << '\n'
      << "  substrate       "
      << runtime::terrainSubstrateName(explanation.context.substrate) << '\n';

  const RuleVerdict *matched = nullptr;
  for (const RuleVerdict &verdict : explanation.verdicts)
    if (!explanation.ruleId.empty() && verdict.rule->id == explanation.ruleId)
      matched = &verdict;

  if (matched != nullptr) {
    out << "\nRule \"" << matched->rule->id << "\" biome="
        << matched->rule->biome << " priority=" << matched->rule->priority
        << " blend=" << fixed(matched->rule->blend) << '\n';
    printConditions(out, *matched);
    return;
  }

  if (explanation.verdicts.empty()) {
    out << "\nThis recipe has no biome rules, so every sample keeps the "
           "default biome plus painted regions.\n";
    return;
  }
  out << (explanation.overridden
              ? "\nRules considered (a painted region overrides them):\n"
              : "\nNo rule matched this sample:\n");
  for (const RuleVerdict &verdict : explanation.verdicts) {
    out << "  " << verdict.rule->id << " biome=" << verdict.rule->biome
        << " priority=" << verdict.rule->priority << " blend="
        << fixed(verdict.rule->blend) << '\n';
    printConditions(out, verdict);
    out << "    -> "
        << (verdict.rejected
                ? std::string("rejected by ") + verdict.rejection
                : std::string("every condition contains the sample"))
        << '\n';
  }
}

int runSeeds(const std::vector<std::string> &args, std::ostream &out,
             std::ostream &error) {
  std::string seedText;
  std::string format = "text";
  for (std::size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "--format" && i + 1 < args.size()) {
      format = args[++i];
      continue;
    }
    if (!args[i].starts_with("--"))
      seedText = args[i];
  }
  if (seedText.empty()) {
    error << "terrain seeds requires a world seed.\n";
    return ExitUsageError;
  }
  if (format != "text" && format != "json") {
    error << "--format must be text or json.\n";
    return ExitUsageError;
  }
  int worldSeed = 0;
  if (!parseInteger(seedText, worldSeed)) {
    error << "terrain seeds requires an integer world seed: " << seedText
          << '\n';
    return ExitUsageError;
  }
  if (format == "json") {
    out << Json{{"format_version", 1},
                {"world_seed", worldSeed},
                {"channels", seedTable(worldSeed)}}
               .dump(2)
        << '\n';
    return 0;
  }
  out << "World seed: " << worldSeed << '\n';
  for (const runtime::TerrainSeedChannel channel : SeedChannels)
    out << "  " << runtime::terrainSeedChannelName(channel) << ' '
        << runtime::deriveTerrainSubSeed(worldSeed, channel) << '\n';
  return 0;
}

int runInspect(const std::vector<std::string> &args, std::ostream &out,
               std::ostream &error) {
  std::string path;
  std::string format = "text";
  for (std::size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "--format" && i + 1 < args.size()) {
      format = args[++i];
      continue;
    }
    if (!args[i].starts_with("--"))
      path = args[i];
  }
  if (path.empty()) {
    error << "terrain inspect requires a recipe path.\n";
    return ExitUsageError;
  }
  if (format != "text" && format != "json") {
    error << "--format must be text or json.\n";
    return ExitUsageError;
  }
  const auto recipe = loadRecipe(path, error);
  if (!recipe)
    return ExitValidationFailure;
  if (format == "json")
    out << inspectToJson(*recipe).dump(2) << '\n';
  else
    printInspectText(*recipe, path, out);
  return 0;
}

int runExplain(const std::vector<std::string> &args, std::ostream &out,
               std::ostream &error) {
  std::string path;
  std::string format = "text";
  std::string at;
  for (std::size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "--format" && i + 1 < args.size()) {
      format = args[++i];
      continue;
    }
    if (args[i] == "--at" && i + 1 < args.size()) {
      at = args[++i];
      continue;
    }
    if (!args[i].starts_with("--"))
      path = args[i];
  }
  if (path.empty()) {
    error << "terrain explain requires a recipe path.\n";
    return ExitUsageError;
  }
  if (at.empty()) {
    error << "terrain explain requires --at <x>,<z>.\n";
    return ExitUsageError;
  }
  if (format != "text" && format != "json") {
    error << "--format must be text or json.\n";
    return ExitUsageError;
  }
  const auto recipe = loadRecipe(path, error);
  if (!recipe)
    return ExitValidationFailure;
  const auto point = parseAt(at, *recipe, error);
  if (!point)
    return ExitUsageError;

  bool completed = false;
  std::optional<runtime::HeightField> field;
  try {
    field = runtime::TerrainGenerator::generate(
        *recipe, {}, [&](float fraction) { completed = fraction >= 1.F; });
  } catch (const std::exception &e) {
    error << "Failed to generate the terrain field: " << e.what() << '\n';
    return ExitValidationFailure;
  }
  if (!field) {
    error << "Terrain generation stopped before the field was complete.\n";
    return ExitValidationFailure;
  }
  if (completed)
    error << "Generated " << recipe->sampleCount() << " terrain samples.\n";

  const auto cell = locateCell(*recipe, point->x, point->z);
  std::size_t index = 0;
  try {
    index = field->index(cell.x, cell.z);
  } catch (const std::out_of_range &) {
    error << "Position " << at << " resolves outside the generated field.\n";
    return ExitValidationFailure;
  }

  runtime::TerrainRuleContextBuilder builder(*recipe);
  std::vector<runtime::TerrainRuleDecision> decisions;
  try {
    const std::vector<float> baseHeights(field->baseHeights.begin(),
                                         field->baseHeights.end());
    builder.build(*recipe, baseHeights, recipe->cellsX, recipe->cellsZ,
                  field->size);
    decisions = runtime::assignTerrainBiomes(*recipe, builder.contexts(),
                                             recipe->cellsX, recipe->cellsZ,
                                             field->size);
  } catch (const std::exception &e) {
    error << "Failed to assign terrain biomes: " << e.what() << '\n';
    return ExitValidationFailure;
  }

  const auto &context = builder.contexts()[index];
  const auto &decision = decisions[index];
  if (decision.biome >= field->biomeIds.size()) {
    error << "Terrain field reported an unknown biome index.\n";
    return ExitValidationFailure;
  }

  Explanation explanation;
  explanation.point = *point;
  explanation.cell = cell;
  explanation.index = index;
  explanation.seaLevel = runtime::TerrainRuleContextBuilder::seaLevel(*recipe);
  explanation.context = context;
  explanation.biome = field->biomeIds[decision.biome];
  explanation.ruleId = decision.ruleId;
  explanation.overridden = decision.overridden;
  explanation.source = decision.overridden
                           ? "region"
                           : (decision.ruleId.empty() ? "default" : "rule");
  for (const runtime::TerrainBiomeRule *rule : activeRules(*recipe))
    explanation.verdicts.push_back(judge(*rule, context));

  if (format == "json")
    out << explainToJson(*recipe, explanation).dump(2) << '\n';
  else
    printExplainText(*recipe, explanation, out);
  return 0;
}

struct ProjectContext {
  std::filesystem::path projectFile;
  std::filesystem::path projectDirectory;
  runtime::ProjectData project;
};

// The project file is located with the shared discovery helper, so a directory
// and a nested path both resolve, and is then parsed by the shared project
// parser so every verb agrees on scene paths and on what a valid project is.
std::optional<ProjectContext> loadProjectContext(const std::string &argument,
                                                 std::ostream &error) {
  const std::filesystem::path requested(argument);
  std::error_code code;
  if (!std::filesystem::exists(requested, code)) {
    error << "Project path does not exist: " << argument << '\n';
    return std::nullopt;
  }
  const std::filesystem::path projectFile = findProjectFile(requested);
  if (projectFile.empty()) {
    error << "No demi.project.json found at or above " << argument << '\n';
    return std::nullopt;
  }
  std::string reason;
  const auto document =
      runtime::scene_loading::readJsonFile(projectFile, reason);
  auto project = document ? runtime::scene_loading::parseProjectData(
                                projectFile, *document, reason)
                          : std::nullopt;
  if (!project) {
    error << "Invalid project " << projectFile.string() << ": "
          << (reason.empty() ? "it could not be read." : reason) << '\n';
    return std::nullopt;
  }
  return ProjectContext{projectFile, project->projectDirectory,
                        std::move(*project)};
}

struct PresetOptions {
  std::string project;
  std::string preset;
  std::string entity;
  std::string format = "text";
};

// Both preset verbs take a project path and --format; apply-preset additionally
// takes --preset and --entity. An unrecognised option is rejected instead of
// silently ignored, so a mistyped flag never becomes a silent no-op.
std::optional<PresetOptions>
parsePresetOptions(const std::vector<std::string> &args, std::ostream &error) {
  PresetOptions options;
  for (std::size_t index = 2; index < args.size(); ++index) {
    if (args[index] == "--format" && index + 1 < args.size()) {
      options.format = args[++index];
      continue;
    }
    if (args[index] == "--preset" && index + 1 < args.size()) {
      options.preset = args[++index];
      continue;
    }
    if (args[index] == "--entity" && index + 1 < args.size()) {
      options.entity = args[++index];
      continue;
    }
    if (!args[index].starts_with("--")) {
      options.project = args[index];
      continue;
    }
    error << "Unknown option: " << args[index] << '\n';
    return std::nullopt;
  }
  if (options.project.empty()) {
    error << "terrain " << args[1] << " requires a project path.\n";
    return std::nullopt;
  }
  if (options.format != "text" && options.format != "json") {
    error << "--format must be text or json.\n";
    return std::nullopt;
  }
  return options;
}

Json presetToJson(const runtime::TerrainPreset &preset) {
  Json biomes = Json::array();
  for (const auto &[id, biome] : preset.biomes) {
    static_cast<void>(biome);
    biomes.push_back(id);
  }
  Json rules = Json::array();
  for (const runtime::TerrainBiomeRule &rule : preset.rules)
    rules.push_back(rule.id);
  return {{"id", preset.id},
          {"name", preset.name},
          {"label", preset.label},
          {"description", preset.description},
          {"preset_version", preset.version},
          {"size", {preset.size.x, preset.size.y}},
          {"resolution", {preset.cellsX, preset.cellsZ}},
          {"chunk_cells", preset.chunkCells},
          {"seed", preset.seed},
          {"default_biome", preset.defaultBiome},
          {"default_landform", preset.defaultLandform},
          {"landform_count", preset.landforms.size()},
          {"biome_count", preset.biomes.size()},
          {"graph_node_count",
           preset.graph.is_null() ? 0 : preset.graph.at("nodes").size()},
          {"rule_count", preset.rules.size()},
          {"rules", std::move(rules)}};
}

void printPresetText(const runtime::TerrainPreset &preset, std::ostream &out) {
  out << "  " << preset.id << '\n'
      << "    Name: " << preset.name << " (\"" << preset.label << "\")\n"
      << "    Size: " << fixed(preset.size.x) << " x " << fixed(preset.size.y)
      << '\n'
      << "    Resolution: " << preset.cellsX << " x " << preset.cellsZ << '\n'
      << "    Chunk cells: " << preset.chunkCells << '\n'
      << "    Seed: " << preset.seed << '\n'
      << "    Default biome: " << preset.defaultBiome << '\n'
      << "    Biomes (" << preset.biomes.size() << "):";
  for (const auto &[id, biome] : preset.biomes) {
    static_cast<void>(biome);
    out << ' ' << id;
  }
  out << '\n'
      << "    Rules (" << preset.rules.size() << "):";
  for (const runtime::TerrainBiomeRule &rule : preset.rules)
    out << ' ' << rule.id;
  out << '\n';
  if (!preset.graph.is_null())
    out << "    Graph nodes: " << preset.graph.at("nodes").size() << '\n';
  if (!preset.description.empty())
    out << "    Description: " << preset.description << '\n';
}

int runPresets(const std::vector<std::string> &args, std::ostream &out,
               std::ostream &error) {
  const auto options = parsePresetOptions(args, error);
  if (!options)
    return ExitUsageError;
  const auto context = loadProjectContext(options->project, error);
  if (!context)
    return ExitValidationFailure;

  const demi::AssetRegistry registry =
      loadAssetRegistry(context->projectDirectory);
  std::vector<std::string> presetIds;
  for (const demi::AssetManifest &manifest : registry.assets) {
    if (manifest.type != "DataAsset")
      continue;
    const auto metadata = assets::dataAssetMetadata(manifest);
    if (metadata && metadata->contentType == PresetContentType)
      presetIds.push_back(manifest.id);
  }
  std::ranges::sort(presetIds);

  auto presets = runtime::builtinTerrainPresets();
  presets.reserve(presetIds.size());
  bool failed = false;
  for (const std::string &id : presetIds) {
    try {
      const auto preset = runtime::loadTerrainPreset(registry, id);
      if (preset)
        presets.push_back(*preset);
    } catch (const std::exception &exception) {
      // One malformed preset must not hide the presets that did load, so the
      // failure is reported and the listing still names the good ones.
      error << exception.what() << '\n';
      failed = true;
    }
  }

  std::ranges::sort(presets, {}, &runtime::TerrainPreset::id);
  if (options->format == "json") {
    Json documents = Json::array();
    for (const runtime::TerrainPreset &preset : presets)
      documents.push_back(presetToJson(preset));
    out << Json{{"format_version", 1},
                {"project", context->projectFile.string()},
                {"count", presets.size()},
                {"presets", std::move(documents)}}
               .dump(2)
        << '\n';
    return failed ? ExitValidationFailure : 0;
  }

  out << "Terrain presets in " << context->projectFile.string() << " ("
      << presets.size() << "):\n";
  if (presets.empty())
    out << "  (none: no DataAsset declares settings.content_type \""
        << PresetContentType << "\")\n";
  for (const runtime::TerrainPreset &preset : presets)
    printPresetText(preset, out);
  return failed ? ExitValidationFailure : 0;
}

// Mutable navigation over the document this verb is about to write, where the
// shared scene_loading helpers are read-only by design.
Json *member(Json &object, std::string_view key) {
  const auto found = object.find(key);
  return found == object.end() ? nullptr : &*found;
}

Json *objectMember(Json &object, std::string_view key) {
  Json *found = member(object, key);
  return found != nullptr && found->is_object() ? found : nullptr;
}

struct TerrainEntity {
  std::string id;
  std::string name;
};

// Candidates come from the expanded runtime world, so nested children and
// prefab instances are seen exactly as the scene loader sees them, and are then
// matched back to authored ids: only an authored entity has a recipe this verb
// can patch in source.
std::vector<TerrainEntity> terrainEntities(const Json &sceneSource,
                                           const runtime::World &world) {
  const Json *entities =
      runtime::scene_loading::arrayField(sceneSource, "entities");
  if (entities == nullptr)
    return {};
  std::vector<TerrainEntity> found;
  for (const runtime::Entity &entity : world.entities) {
    if (runtime::composition::findAuthoredEntity(*entities, entity.id) ==
        nullptr)
      continue;
    const bool hasTerrain = std::ranges::any_of(
        entity.authoredComponents,
        [](const auto &component) { return component->name() == TerrainComponent; });
    if (hasTerrain)
      found.push_back({entity.id, entity.name});
  }
  std::ranges::sort(found, {}, &TerrainEntity::id);
  return found;
}

std::string joinIds(const std::vector<TerrainEntity> &entities) {
  std::string joined;
  for (const TerrainEntity &entity : entities) {
    if (!joined.empty())
      joined += ", ";
    joined += entity.id;
  }
  return joined;
}

std::optional<std::string> readTextFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::nullopt;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

int runApplyPreset(const std::vector<std::string> &args, std::ostream &out,
                   std::ostream &error) {
  const auto options = parsePresetOptions(args, error);
  if (!options)
    return ExitUsageError;
  if (options->preset.empty()) {
    error << "terrain apply-preset requires --preset "
             "<asset://id|builtin://terrain/name>.\n";
    return ExitUsageError;
  }
  const auto context = loadProjectContext(options->project, error);
  if (!context)
    return ExitValidationFailure;

  const demi::AssetRegistry registry =
      loadAssetRegistry(context->projectDirectory);
  std::optional<runtime::TerrainPreset> preset;
  try {
    preset = runtime::loadTerrainPreset(registry, options->preset);
  } catch (const std::exception &exception) {
    error << exception.what() << '\n';
    return ExitValidationFailure;
  }
  if (!preset) {
    error << "Terrain preset was not found: " << options->preset << '\n';
    return ExitValidationFailure;
  }

  const auto sceneEntry =
      std::ranges::find_if(context->project.scenes,
                           [&](const runtime::SceneEntry &entry) {
                             return entry.id == context->project.mainScene;
                           });
  if (sceneEntry == context->project.scenes.end()) {
    error << "main_scene does not match any scene entry: "
          << context->project.mainScene << '\n';
    return ExitValidationFailure;
  }
  const std::filesystem::path scenePath =
      context->projectDirectory / sceneEntry->path;

  const auto sourceText = readTextFile(scenePath);
  if (!sourceText) {
    error << "Failed to read scene " << scenePath.string() << '\n';
    return ExitValidationFailure;
  }
  Json sceneSource;
  try {
    sceneSource = Json::parse(*sourceText);
  } catch (const Json::parse_error &parseError) {
    error << "Invalid JSON in " << scenePath.string() << ": "
          << parseError.what() << '\n';
    return ExitValidationFailure;
  }

  const runtime::composition::ExpansionResult expansion =
      runtime::composition::expandScene(scenePath, sceneSource, false);
  if (!expansion.document) {
    error << "Scene could not be composed: " << scenePath.string();
    if (!expansion.diagnostics.empty())
      error << ": " << expansion.diagnostics.front().message;
    error << '\n';
    return ExitValidationFailure;
  }
  const runtime::World world = runtime::scene_loading::parseSceneWorld(
      scenePath, *expansion.document);
  const std::vector<TerrainEntity> candidates =
      terrainEntities(sceneSource, world);

  std::string targetId;
  if (!options->entity.empty()) {
    const auto selected = std::ranges::find_if(
        candidates, [&](const TerrainEntity &candidate) {
          return candidate.id == options->entity;
        });
    if (selected == candidates.end()) {
      error << "Entity " << options->entity << " in " << scenePath.string()
            << " has no " << TerrainComponent << " component.\n";
      if (!candidates.empty())
        error << "Terrain entities: " << joinIds(candidates) << '\n';
      return ExitValidationFailure;
    }
    targetId = selected->id;
  } else if (candidates.empty()) {
    error << "No " << TerrainComponent << " entity in " << scenePath.string()
          << " to apply a preset to.\n";
    return ExitValidationFailure;
  } else if (candidates.size() > 1) {
    error << "Scene " << scenePath.string() << " has " << candidates.size()
          << ' ' << TerrainComponent << " entities (" << joinIds(candidates)
          << "); pass --entity <id>.\n";
    return ExitUsageError;
  } else {
    targetId = candidates.front().id;
  }

  Json merged = sceneSource;
  Json *entities = member(merged, "entities");
  Json *entity = entities == nullptr || !entities->is_array()
                     ? nullptr
                     : runtime::composition::findAuthoredEntity(*entities,
                                                                  targetId);
  Json *components =
      entity == nullptr ? nullptr : objectMember(*entity, "components");
  Json *terrain = components == nullptr
                      ? nullptr
                      : objectMember(*components, TerrainComponent);
  Json *recipe =
      terrain == nullptr ? nullptr : objectMember(*terrain, "recipe");
  if (recipe == nullptr) {
    error << "Entity " << targetId << " in " << scenePath.string()
          << " has no Terrain3D recipe.\n";
    return ExitValidationFailure;
  }

  try {
    *recipe = runtime::applyTerrainPreset(*recipe, *preset);
  } catch (const std::exception &exception) {
    error << "Failed to apply preset " << preset->id << " to " << targetId
          << ": " << exception.what() << '\n';
    return ExitValidationFailure;
  }

  // The source-preserving patcher is what keeps an applied preset reviewable:
  // it rewrites only the changed spans, and the full document is written only
  // when the change cannot be expressed as a small diff.
  const std::optional<std::string> patched =
      demi::filesystem::patchAuthoredJsonSource(*sourceText, sceneSource, merged);
  const std::string text = patched.value_or(merged.dump(2) + '\n');

  // Written through the shared atomic helper, so a failure leaves the previous
  // source byte-identical instead of a half-written scene.
  std::error_code code;
  if (!atomicWriteText(scenePath, text, code)) {
    error << "Failed to write scene " << scenePath.string() << ": "
          << code.message() << '\n';
    return ExitValidationFailure;
  }

  if (options->format == "json") {
    out << Json{{"format_version", 1},
                {"project", context->projectFile.string()},
                {"scene", scenePath.string()},
                {"entity", targetId},
                {"preset_id", preset->id},
                {"preset_version", preset->version},
                {"recipe", *recipe},
                {"write", patched ? "patched" : "rewritten"}}
               .dump(2)
        << '\n';
    return 0;
  }
  out << "Applied preset " << preset->id << " (version " << preset->version
      << ") to " << targetId << " in " << scenePath.string() << '\n'
      << "  Seed: " << recipe->value("seed", 0) << '\n'
      << "  Default biome: " << recipe->value("default_biome", std::string{})
      << '\n'
      << "  Landforms: " << recipe->value("landforms", Json::object()).size()
      << ", biomes: " << recipe->value("biomes", Json::object()).size()
      << ", layers: " << recipe->value("layers", Json::array()).size()
      << ", rules: " << recipe->value("rules", Json::array()).size() << '\n'
      << "  Kept regions: " << recipe->value("regions", Json::array()).size()
      << ", edits: " << recipe->value("edits", Json::array()).size()
      << ", exclusions: " << recipe->value("exclusions", Json::array()).size()
      << '\n'
      << "  Wrote "
      << (patched ? "a patched source diff" : "a rewritten document") << '\n';
  return 0;
}

} // namespace

int runValidate(const std::vector<std::string> &args, std::ostream &out,
                std::ostream &error);

int runTerrainCommand(const std::vector<std::string> &args, std::ostream &out,
                      std::ostream &error) {
  if (args.size() < 2) {
    error << "Usage: demi terrain inspect <recipe.json> "
             "[--format text|json]\n"
             "       demi terrain compact <recipe-or-terrain.json> [--write]\n"
             "       demi terrain explain <recipe.json> --at <x>,<z> "
             "[--format text|json]\n"
             "       demi terrain seeds <seed> [--format text|json]\n"
          << "       demi terrain presets <project> [--format text|json]\n"
          << "       demi terrain apply-preset <project> --preset "
             "<asset://id|builtin://terrain/name> "
             "[--entity <id>] [--format text|json]\n"
          << "       demi terrain validate <project> [--palette <asset://id>] "
             "[--materials <asset://id>] [--development] [--json]\n";
    return ExitUsageError;
  }
  const std::string &subcommand = args[1];
  if (subcommand == "compact")
    return runTerrainCompactCommand(args, out, error);
  if (subcommand == "inspect")
    return runInspect(args, out, error);
  if (subcommand == "explain")
    return runExplain(args, out, error);
  if (subcommand == "seeds")
    return runSeeds(args, out, error);
  if (subcommand == "presets")
    return runPresets(args, out, error);
  if (subcommand == "apply-preset")
    return runApplyPreset(args, out, error);
  if (subcommand == "validate")
    return runValidate(args, out, error);
  error << "Unknown terrain subcommand: " << subcommand << '\n'
        << "Usage: demi terrain "
           "inspect|explain|seeds|presets|apply-preset|validate ...\n";
  return ExitUsageError;
}


int runValidate(const std::vector<std::string> &args, std::ostream &out,
                std::ostream &error) {
  if (args.size() < 3) {
    error << "Usage: demi terrain validate <project> [--palette <asset://id>] "
             "[--materials <asset://id>] [--development] [--json]\n";
    return ExitUsageError;
  }
  const auto context = loadProjectContext(args[2], error);
  if (!context)
    return ExitValidationFailure;
  const demi::AssetRegistry registry =
      loadAssetRegistry(context->projectDirectory);

  demi::assets::TerrainAssetValidationRequest request;
  request.registry = &registry;
  request.use = hasArg(args, "--development")
                    ? demi::assets::TerrainAssetUse::DevelopmentOnly
                    : demi::assets::TerrainAssetUse::Shippable;
  if (const auto palette = valueAfter(args, "--palette"); !palette.empty())
    request.paletteId = palette;
  if (const auto materials = valueAfter(args, "--materials");
      !materials.empty())
    request.materialSetId = materials;
  if (request.paletteId.empty() && request.materialSetId.empty()) {
    // Fall back to whatever the project declares, so the common case needs no
    // flags at all and cannot be run against the wrong document by accident.
    for (const demi::AssetManifest &asset : registry.assets) {
      const auto metadata = assets::dataAssetMetadata(asset);
      if (!metadata)
        continue;
      if (metadata->contentType == "terrain_palette" &&
          request.paletteId.empty())
        request.paletteId = asset.id;
      if (metadata->contentType == "terrain_material_set" &&
          request.materialSetId.empty())
        request.materialSetId = asset.id;
    }
  }
  if (request.paletteId.empty() && request.materialSetId.empty()) {
    error << "No terrain palette or material set found in " << args[2]
          << ". Name one with --palette or --materials.\n";
    return ExitValidationFailure;
  }

  const auto report = demi::assets::validateTerrainAssets(request);
  if (hasArg(args, "--json")) {
    Json findings = Json::array();
    for (const auto &finding : report.findings)
      findings.push_back({{"kind",
                           demi::assets::terrainAssetFindingKindName(
                               finding.kind)},
                          {"role", finding.role},
                          {"asset", finding.assetId},
                          {"blocking", report.blocking(finding)},
                          {"message", finding.message}});
    out << Json{{"format_version", 1},
                {"use", demi::assets::terrainAssetUseName(report.use)},
                {"checked", report.checked},
                {"blocking", report.blockingCount()},
                {"advisory", report.advisoryCount()},
                {"shippable", report.shippable()},
                {"findings", std::move(findings)}}
            .dump(2)
        << '\n';
  } else {
    out << report.summary() << '\n';
    for (const auto &finding : report.findings)
      out << (report.blocking(finding) ? "  error   " : "  warning ")
          << demi::assets::terrainAssetFindingKindName(finding.kind) << ": "
          << finding.message << '\n';
  }
  // Findings do not fail the command; the exit code answers "may I ship".
  return report.shippable() ? 0 : ExitValidationFailure;
}

} // namespace demi::cli
