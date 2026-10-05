#include "cli/TerrainCommands.h"

#include "demi/assets/AssetHash.h"
#include "demi/runtime/scene/ProjectParser.h"
#include "demi/runtime/scene/SceneEntityParser.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "demi/runtime/terrain/TerrainSeed.h"
#include "demi/schema/Validation.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int ExitValidationFailure = 1;
constexpr int ExitUsageError = 2;

struct Run {
  int code = 0;
  std::string out;
  std::string error;
};

Run invoke(const std::vector<std::string> &args) {
  std::ostringstream out;
  std::ostringstream error;
  Run run;
  run.code = demi::cli::runTerrainCommand(args, out, error);
  run.out = out.str();
  run.error = error.str();
  return run;
}

std::filesystem::path testRoot() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("demi_terrain_cli_" + std::to_string(nonce));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  return root;
}

std::string writeRecipe(const std::filesystem::path &root,
                        const std::string &name, const std::string &body) {
  const auto path = root / name;
  std::ofstream output(path);
  output << body;
  output.close();
  return path.string();
}

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

// Nine significant digits round-trip a float, so a probed threshold authored
// into a recipe is exactly the value the field reported.
std::string exactNumber(float value) {
  std::ostringstream stream;
  stream << std::setprecision(9) << value;
  return stream.str();
}

constexpr const char *PlainRecipe = R"({
  "format_version": 1,
  "size": [64, 64],
  "resolution": [32, 32],
  "seed": 4242,
  "chunk_cells": 16,
  "default_biome": "meadow",
  "biomes": {
    "meadow": {"landform": "meadow", "color": [0.3, 0.5, 0.2, 1.0]},
    "rock": {"landform": "rock", "color": [0.5, 0.5, 0.5, 1.0]}
  },
  "landforms": {
    "default": {"base_height": 0.0, "height_variation": 4.0, "feature_size": 24},
    "meadow": {"base_height": 0.0, "height_variation": 10.0, "feature_size": 24},
    "rock": {"base_height": 14.0, "height_variation": 8.0, "feature_size": 18}
  },
  "default_landform": "default"
})";

// A rule that only claims high ground, so a low sample must fall through to the
// default biome and explain why.
constexpr const char *RuleRecipe = R"({
  "format_version": 1,
  "size": [64, 64],
  "resolution": [32, 32],
  "seed": 4242,
  "default_biome": "meadow",
  "biomes": {
    "meadow": {"landform": "meadow", "color": [0.3, 0.5, 0.2, 1.0]},
    "rock": {"landform": "rock", "color": [0.5, 0.5, 0.5, 1.0]}
  },
  "landforms": {
    "default": {"base_height": 0.0, "height_variation": 4.0, "feature_size": 24},
    "meadow": {"base_height": 0.0, "height_variation": 10.0, "feature_size": 24},
    "rock": {"base_height": 14.0, "height_variation": 8.0, "feature_size": 18}
  },
  "default_landform": "default",
  "rules": [
    {"id": "summit", "biome": "rock", "priority": 20, "blend": 2,
     "elevation": [0.01, 1e6], "slope": [0, 45]}
  ]
})";

void inspectTextAndJson(const std::filesystem::path &root) {
  const auto path = writeRecipe(root, "plain.json", PlainRecipe);
  const auto text = invoke({"terrain", "inspect", path});
  assert(text.code == 0);
  assert(text.error.empty());
  assert(contains(text.out, "Size: 64.000 x 64.000"));
  assert(contains(text.out, "Resolution: 32 x 32 (1089 samples)"));
  assert(contains(text.out, "Seed: 4242"));
  assert(contains(text.out, "Chunk cells: 16"));
  assert(contains(text.out, "Default biome: meadow"));
  assert(contains(text.out, "Biomes (2):"));
  assert(contains(text.out, "meadow"));
  assert(contains(text.out, "rock"));
  assert(contains(text.out, "Layers (5):"));
  assert(contains(text.out, "kind=biome"));
  assert(contains(text.out, "Rules (0):"));
  assert(contains(text.out, "regions: 0"));
  assert(contains(text.out, "Sub-seeds from world seed 4242:"));
  assert(contains(text.out, "landform "));

  const auto json = invoke({"terrain", "inspect", path, "--format", "json"});
  assert(json.code == 0);
  const auto document = nlohmann::json::parse(json.out);
  assert(document.at("format_version") == 1);
  assert(document.at("seed") == 4242);
  assert(document.at("resolution").at(0) == 32);
  assert(document.at("sample_count") == 1089);
  assert(document.at("default_biome") == "meadow");
  assert(document.at("biomes").size() == 2);
  assert(document.at("layers").size() == 5);
  assert(document.at("rules").empty());
  assert(document.at("strokes").at("regions") == 0);
  assert(document.at("sub_seeds").size() == 5);
}

void inspectReportsRuleConditions(const std::filesystem::path &root) {
  const auto path = writeRecipe(root, "rules.json", RuleRecipe);
  const auto text = invoke({"terrain", "inspect", path});
  assert(text.code == 0);
  assert(contains(text.out, "Rules (1):"));
  assert(contains(text.out,
                  "summit biome=rock priority=20 layer=biomes blend=2.000"));
  assert(contains(text.out, "elevation      [0.010,1000000.000]"));
  assert(contains(text.out, "slope          [0.000,45.000]"));
  assert(contains(text.out, "moisture       unconstrained"));
  assert(contains(text.out, "water distance unconstrained"));
  assert(contains(text.out, "substrate      unconstrained"));

  const auto json = invoke({"terrain", "inspect", path, "--format", "json"});
  assert(json.code == 0);
  const auto document = nlohmann::json::parse(json.out);
  assert(document.at("rules").size() == 1);
  const auto &rule = document.at("rules").at(0);
  assert(rule.at("id") == "summit");
  assert(rule.at("biome") == "rock");
  assert(rule.at("priority") == 20);
  assert(rule.at("blend") == 2);
  assert(rule.at("elevation").at(1) == 1e6);
  assert(rule.at("slope").at(0) == 0);
  // An unconstrained band is null, never an invented default.
  assert(rule.at("moisture").is_null());
  assert(rule.at("water_distance").is_null());
}

void inspectRejectsInvalidRecipe(const std::filesystem::path &root) {
  const auto path = writeRecipe(
      root, "invalid.json", R"({"format_version": 1, "size": [0, 64]})");
  const auto run = invoke({"terrain", "inspect", path});
  assert(run.code == ExitValidationFailure);
  assert(run.out.empty());
  assert(contains(run.error, "Invalid terrain recipe"));
}

void explainWithoutRules(const std::filesystem::path &root) {
  const auto path = writeRecipe(root, "plain.json", PlainRecipe);
  const auto run =
      invoke({"terrain", "explain", path, "--at", "32,32", "--format", "text"});
  assert(run.code == 0);
  assert(contains(run.out, "Biome: "));
  assert(contains(run.out, "Source: default biome (no rule matched"));
  assert(contains(run.out, "substrate"));
  assert(contains(run.out, "This recipe has no biome rules"));

  const auto json = invoke(
      {"terrain", "explain", path, "--at", "32,32", "--format", "json"});
  assert(json.code == 0);
  const auto document = nlohmann::json::parse(json.out);
  assert(document.at("source") == "default");
  assert(document.at("rule_id").is_null());
  assert(document.at("overridden") == false);
  assert(document.at("candidates").empty());
  assert(document.at("cell").at("x") == 16);
  assert(document.at("cell").at("z") == 16);
}

// Rules band on elevation, so the test must not hardcode a threshold against
// Perlin output statistics. Probe the generated field for its own extremes and
// ask about the samples that produced them.
struct Extreme {
  float height = 0;
  std::string at;
};

std::pair<Extreme, Extreme> probeExtremes() {
  const auto recipe = demi::runtime::TerrainRecipe::parse(
      nlohmann::json::parse(PlainRecipe));
  const auto field = demi::runtime::TerrainGenerator::generate(recipe);
  assert(field);
  Extreme high{field->baseHeights.front(), "0,0"};
  Extreme low{field->baseHeights.front(), "0,0"};
  for (int z = 0; z <= recipe.cellsZ; ++z)
    for (int x = 0; x <= recipe.cellsX; ++x) {
      const float height = field->baseHeights[field->index(x, z)];
      const auto position = field->position(x, z);
      std::ostringstream at;
      at << position.x << ',' << position.y;
      if (height > high.height)
        high = Extreme{height, at.str()};
      if (height < low.height)
        low = Extreme{height, at.str()};
    }
  assert(high.height > low.height);
  return {high, low};
}

void explainNamesTheRuleThatAssignedTheSample(
    const std::filesystem::path &root) {
  const auto [high, low] = probeExtremes();
  const auto path = writeRecipe(
      root, "elevation.json",
      std::string(R"({
  "format_version": 1,
  "size": [64, 64],
  "resolution": [32, 32],
  "seed": 4242,
  "default_biome": "meadow",
  "biomes": {
    "meadow": {"landform": "meadow", "color": [0.3, 0.5, 0.2, 1.0]},
    "rock": {"landform": "rock", "color": [0.5, 0.5, 0.5, 1.0]}
  },
  "landforms": {
    "default": {"base_height": 0.0, "height_variation": 4.0, "feature_size": 24},
    "meadow": {"base_height": 0.0, "height_variation": 10.0, "feature_size": 24},
    "rock": {"base_height": 14.0, "height_variation": 8.0, "feature_size": 18}
  },
  "default_landform": "default",
  "rules": [
    {"id": "summit", "biome": "rock", "priority": 20, "elevation": [)")
          + exactNumber(high.height) + ", 1e6]}\n  ]\n}");

  const auto matched =
      invoke({"terrain", "explain", path, "--at", high.at, "--format", "text"});
  assert(matched.code == 0);
  assert(contains(matched.out, "Biome: rock"));
  assert(contains(matched.out, "Source: rule \"summit\""));
  assert(contains(matched.out, "Rule \"summit\" biome=rock priority=20"));
  assert(contains(matched.out, "INSIDE"));
  assert(!contains(matched.out, "OUTSIDE"));

  // A rule that only claims the top of the field must leave the lowest point on
  // the default biome, and must say which condition rejected it.
  const auto rejected =
      invoke({"terrain", "explain", path, "--at", low.at, "--format", "text"});
  assert(rejected.code == 0);
  assert(contains(rejected.out, "Biome: meadow"));
  assert(contains(rejected.out, "Source: default biome (no rule matched"));
  assert(contains(rejected.out, "No rule matched this sample:"));
  assert(contains(rejected.out, "rejected by elevation"));

  const auto json = invoke(
      {"terrain", "explain", path, "--at", high.at, "--format", "json"});
  assert(json.code == 0);
  const auto document = nlohmann::json::parse(json.out);
  assert(document.at("biome") == "rock");
  assert(document.at("source") == "rule");
  assert(document.at("rule_id") == "summit");
  assert(document.at("candidates").size() == 1);
  assert(document.at("candidates").at(0).at("selected") == true);
}

void explainReportsPaintedRegionOverride(const std::filesystem::path &root) {
  const auto path = writeRecipe(
      root, "override.json", R"({
  "format_version": 1,
  "size": [64, 64],
  "resolution": [32, 32],
  "seed": 4242,
  "default_biome": "meadow",
  "biomes": {
    "meadow": {"landform": "meadow", "color": [0.3, 0.5, 0.2, 1.0]},
    "rock": {"landform": "rock", "color": [0.5, 0.5, 0.5, 1.0]}
  },
  "landforms": {
    "default": {"base_height": 0.0, "height_variation": 4.0, "feature_size": 24},
    "meadow": {"base_height": 0.0, "height_variation": 10.0, "feature_size": 24},
    "rock": {"base_height": 14.0, "height_variation": 8.0, "feature_size": 18}
  },
  "default_landform": "default",
  "regions": [
    {"biome": "rock", "center": [32, 32], "radius": 64, "strength": 1, "falloff": 0}
  ],
  "rules": [
    {"id": "everywhere", "biome": "meadow", "priority": 1000}
  ]
})");
  const auto run =
      invoke({"terrain", "explain", path, "--at", "32,32", "--format", "text"});
  assert(run.code == 0);
  assert(contains(run.out, "Source: painted region override"));
  assert(!contains(run.out, "Source: rule "));
  assert(!contains(run.out, "Biome: meadow"));

  const auto json = invoke(
      {"terrain", "explain", path, "--at", "32,32", "--format", "json"});
  assert(json.code == 0);
  const auto document = nlohmann::json::parse(json.out);
  assert(document.at("overridden") == true);
  assert(document.at("source") == "region");
  // A painted region is authoritative, so no rule may be named as the reason.
  assert(document.at("rule_id").is_null());
  assert(document.at("rule").is_null());
}

void explainAtBoundsClampToLastCell(const std::filesystem::path &root) {
  const auto path = writeRecipe(root, "plain.json", PlainRecipe);
  const auto run = invoke(
      {"terrain", "explain", path, "--at", "64,64", "--format", "json"});
  assert(run.code == 0);
  const auto document = nlohmann::json::parse(run.out);
  assert(document.at("cell").at("x") == 32);
  assert(document.at("cell").at("z") == 32);
  assert(document.at("cell").at("index") == 32 * 33 + 32);
}

void explainAtRejectsBadInput(const std::filesystem::path &root) {
  const auto path = writeRecipe(root, "plain.json", PlainRecipe);

  const auto outOfBounds =
      invoke({"terrain", "explain", path, "--at", "65,0"});
  assert(outOfBounds.code == ExitUsageError);
  assert(outOfBounds.out.empty());
  assert(contains(outOfBounds.error, "outside the terrain bounds"));

  const auto negative =
      invoke({"terrain", "explain", path, "--at", "-1,0"});
  assert(negative.code == ExitUsageError);
  assert(contains(negative.error, "outside the terrain bounds"));

  const auto malformed = invoke({"terrain", "explain", path, "--at", "12"});
  assert(malformed.code == ExitUsageError);
  assert(contains(malformed.error, "--at must be two comma-separated numbers"));
  assert(!contains(malformed.error, "outside the terrain bounds"));

  const auto nonFinite =
      invoke({"terrain", "explain", path, "--at", "nan,0"});
  assert(nonFinite.code == ExitUsageError);
  assert(contains(nonFinite.error, "must be finite numbers"));
  assert(!contains(nonFinite.error, "outside the terrain bounds"));

  const auto missing = invoke({"terrain", "explain", path});
  assert(missing.code == ExitUsageError);
  assert(contains(missing.error, "--at"));
}

void seedsTextAndJson() {
  const auto text = invoke({"terrain", "seeds", "1337"});
  assert(text.code == 0);
  assert(contains(text.out, "World seed: 1337"));
  for (const auto channel :
       {demi::runtime::TerrainSeedChannel::Landform,
        demi::runtime::TerrainSeedChannel::Erosion,
        demi::runtime::TerrainSeedChannel::Hydrology,
        demi::runtime::TerrainSeedChannel::BiomePlacement,
        demi::runtime::TerrainSeedChannel::Scatter}) {
    const std::string name(demi::runtime::terrainSeedChannelName(channel));
    const std::string expected =
        "  " + name + " " +
        std::to_string(demi::runtime::deriveTerrainSubSeed(1337, channel)) +
        "\n";
    assert(contains(text.out, expected));
  }

  const auto json = invoke({"terrain", "seeds", "1337", "--format", "json"});
  assert(json.code == 0);
  const auto document = nlohmann::json::parse(json.out);
  assert(document.at("format_version") == 1);
  assert(document.at("world_seed") == 1337);
  assert(document.at("channels").size() == 5);
  assert(document.at("channels").at(0).at("channel") == "landform");
  assert(document.at("channels").at(0).at("seed") ==
         demi::runtime::deriveTerrainSubSeed(
             1337, demi::runtime::TerrainSeedChannel::Landform));

  assert(invoke({"terrain", "seeds"}).code == ExitUsageError);
  assert(invoke({"terrain", "seeds", "not-a-seed"}).code == ExitUsageError);
  assert(invoke({"terrain", "seeds", "1337", "--format", "xml"}).code ==
         ExitUsageError);
}

void usageErrors() {
  assert(invoke({"terrain"}).code == ExitUsageError);
  const auto unknown = invoke({"terrain", "explain", "--at", "0,0"});
  assert(unknown.code == ExitUsageError);
  assert(contains(unknown.error, "Unknown terrain subcommand: explain")
             || contains(unknown.error, "terrain explain requires"));

  const auto unknownSubcommand = invoke({"terrain", "teleport"});
  assert(unknownSubcommand.code == ExitUsageError);
  assert(contains(unknownSubcommand.error, "Unknown terrain subcommand"));
  assert(unknownSubcommand.out.empty());

  const auto missingPath = invoke({"terrain", "inspect"});
  assert(missingPath.code == ExitUsageError);
  assert(contains(missingPath.error, "terrain inspect requires a recipe path"));
}

// A scene document is not a recipe. It must fail with a message, not a crash.
void sceneIsNotARecipe(const std::filesystem::path &root) {
  const auto path = writeRecipe(
      root, "scene.json",
      R"({"format_version": 1, "id": "scene://test/main", "name": "Main", "entities": []})");
  const auto run = invoke({"terrain", "inspect", path});
  assert(run.code == ExitValidationFailure);
  assert(run.out.empty());
  assert(contains(run.error, "Invalid terrain recipe"));
  assert(contains(run.error, "scene document"));

  const auto missing = invoke({"terrain", "inspect", root / "absent.json"});
  assert(missing.code == ExitValidationFailure);
  assert(contains(missing.error, "Failed to read terrain file"));
}

void outputIsDeterministic(const std::filesystem::path &root) {
  const auto path = writeRecipe(root, "rules.json", RuleRecipe);
  const auto first = invoke({"terrain", "inspect", path});
  const auto second = invoke({"terrain", "inspect", path});
  assert(first.code == 0 && second.code == 0);
  assert(first.out == second.out);
  const auto firstJson =
      invoke({"terrain", "inspect", path, "--format", "json"});
  const auto secondJson =
      invoke({"terrain", "inspect", path, "--format", "json"});
  assert(firstJson.out == secondJson.out);
}

constexpr const char *PresetDocument = R"({
  "format_version": 1,
  "name": "Test Ridge",
  "label": "Ridge",
  "description": "Two biomes and two rules.",
  "size": [96.0, 96.0],
  "cells_x": 48,
  "cells_z": 48,
  "chunk_cells": 16,
  "seed": 4242,
  "default_biome": "rock",
  "default_landform": "rock",
  "biomes": {
    "rock": {"landform": "rock", "color": [0.4, 0.38, 0.35, 1.0]},
    "snow": {"landform": "snow", "color": [0.9, 0.93, 0.96, 1.0]}
  },
  "landforms": {
    "default": {"base_height": 6.0, "height_variation": 8.0, "feature_size": 34.0,
                "roughness": 0.5, "octaves": 4},
    "rock": {"base_height": 20.0, "height_variation": 18.0, "feature_size": 40.0,
             "roughness": 0.5, "octaves": 4},
    "snow": {"base_height": 12.0, "height_variation": 6.0, "feature_size": 30.0,
             "roughness": 0.4, "octaves": 3}
  },
  "rules": [
    {"id": "snowline", "layer": "biomes", "biome": "snow", "priority": 50,
     "blend": 4, "elevation": [26, 90]},
    {"id": "cliff_rock", "layer": "biomes", "biome": "rock", "priority": 60,
     "blend": 3, "substrate": ["rock"]}
  ]
})";

constexpr const char *PresetSchemaDocument = R"({
  "format_version": 1,
  "type": "object",
  "required": ["format_version", "name"],
  "properties": {
    "format_version": { "type": "integer", "enum": [1] },
    "name": { "type": "string" }
  }
})";

// The region paints "rock", a biome the test preset also defines, so the merged
// recipe stays valid. A painted region is authorial work a preset must keep.
constexpr const char *PresetScene = R"({
  "format_version": 1,
  "id": "scene://test/main",
  "name": "Preset CLI",
  "entities": [
    {
      "id": "terrain",
      "name": "Terrain",
      "components": {
        "Transform3D": {
          "position": [
            -48,
            0,
            -48
          ]
        },
        "Terrain3D": {
          "recipe": {
            "format_version": 1,
            "size": [
              64,
              64
            ],
            "resolution": [
              32,
              32
            ],
            "seed": 1337,
            "chunk_cells": 16,
            "default_biome": "meadow",
            "default_landform": "default",
            "biomes": {
              "meadow": {
                "landform": "meadow",
                "color": [
                  0.32,
                  0.52,
                  0.22,
                  1
                ]
              },
              "rock": {
                "landform": "rock",
                "color": [
                  0.5,
                  0.47,
                  0.42,
                  1
                ]
              }
            },
            "landforms": {
              "default": {
                "base_height": 0.0,
                "height_variation": 6.0,
                "feature_size": 24.0
              },
              "meadow": {
                "base_height": 0.0,
                "height_variation": 6.0,
                "feature_size": 24.0
              },
              "rock": {
                "base_height": 12.0,
                "height_variation": 9.0,
                "feature_size": 18.0
              }
            },
            "regions": [
              {
                "biome": "rock",
                "center": [
                  20,
                  20
                ],
                "radius": 12,
                "strength": 1,
                "falloff": 0.5
              }
            ],
            "edits": [
              {
                "type": "flatten",
                "center": [
                  40,
                  40
                ],
                "radius": 10,
                "strength": 1,
                "falloff": 0,
                "target_height": 4
              }
            ]
          }
        }
      }
    },
    {
      "id": "marker",
      "name": "Marker",
      "components": {
        "Transform3D": {
          "position": [
            0,
            1,
            0
          ]
        }
      }
    }
  ]
})";

constexpr const char *ProjectDocument = R"({
  "format_version": 1,
  "name": "Preset CLI",
  "main_scene": "scene://test/main",
  "scenes": [{"id": "scene://test/main"}]
})";

constexpr const char *PresetId = "asset://terrain/presets/test_ridge";
constexpr const char *PresetSchemaId = "asset://schemas/terrain_preset";

void writeFile(const std::filesystem::path &root, const std::string &name,
               const std::string &body) {
  const auto path = root / name;
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << body;
  output.close();
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

// A manifest only counts as current when its source_hash is the engine's own
// hashFiles value, so fixtures hash their source exactly as an import would.
std::string dataManifest(const std::string &id, const std::string &type,
                         const std::string &importer, const std::string &source,
                         const std::filesystem::path &sourcePath,
                         const nlohmann::json &settings,
                         const std::vector<std::string> &dependencies) {
  const auto hash = demi::assets::hashFiles({sourcePath});
  assert(hash.has_value());
  return nlohmann::json{{"dependencies", dependencies},
                         {"format_version", 1},
                         {"id", id},
                         {"importer", importer},
                         {"importer_version", 1},
                         {"settings", settings},
                         {"source", source},
                         {"source_hash", *hash},
                         {"type", type}}
      .dump(2);
}

std::filesystem::path makeProject(const std::filesystem::path &root,
                                  const std::string &name,
                                  const std::string &scene,
                                  const bool withPreset) {
  const auto project = root / name;
  std::filesystem::create_directories(project);
  writeFile(project, "demi.project.json", ProjectDocument);
  writeFile(project, "scenes/main.scene.json", scene);
  if (withPreset) {
    const auto schema = project / "assets/terrain/schemas/terrain_preset.schema.json";
    writeFile(project, "assets/terrain/schemas/terrain_preset.schema.json",
              PresetSchemaDocument);
    writeFile(project, "assets/terrain/schemas/terrain_preset.asset.json",
              dataManifest(PresetSchemaId, "DataSchema", "json_schema",
                           "terrain_preset.schema.json", schema,
                           nlohmann::json::object(), {}));
    const auto document = project / "assets/terrain/presets/test_ridge.json";
    writeFile(project, "assets/terrain/presets/test_ridge.json", PresetDocument);
    writeFile(project, "assets/terrain/presets/test_ridge.asset.json",
              dataManifest(PresetId, "DataAsset", "json_data", "test_ridge.json",
                           document,
                           nlohmann::json{{"content_type", "terrain_preset"},
                                          {"schema", PresetSchemaId}},
                           {PresetSchemaId}));
  }
  return project;
}

std::optional<nlohmann::json>
sceneRecipe(const std::filesystem::path &project,
            const std::string &entityId = "terrain") {
  const auto document =
      nlohmann::json::parse(readFile(project / "scenes/main.scene.json"));
  for (const auto &entity : document.at("entities"))
    if (entity.value("id", "") != entityId)
      continue;
    else if (const auto components = entity.find("components");
        components != entity.end())
      if (const auto terrain = components->find("Terrain3D");
          terrain != components->end())
        if (const auto recipe = terrain->find("recipe");
            recipe != terrain->end())
          return *recipe;
  return std::nullopt;
}

std::vector<std::string> textLines(const std::string &text) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line))
    lines.push_back(line);
  return lines;
}

void presetsListProjectPresets(const std::filesystem::path &root) {
  const auto project = makeProject(root, "with_preset", PresetScene, true);
  const auto text = invoke({"terrain", "presets", project.string()});
    assert(text.code == 0);
  assert(text.error.empty());
  assert(contains(text.out, "Terrain presets in "));
  assert(contains(text.out, PresetId));
  assert(contains(text.out, "Name: Test Ridge (\"Ridge\")"));
  assert(contains(text.out, "Description: Two biomes and two rules."));
  assert(contains(text.out, "Size: 96.000 x 96.000"));
  assert(contains(text.out, "Resolution: 48 x 48"));
  assert(contains(text.out, "Seed: 4242"));
  assert(contains(text.out, "Biomes (2): rock snow"));
  assert(contains(text.out, "Rules (2): snowline cliff_rock"));

  const auto json =
      invoke({"terrain", "presets", project.string(), "--format", "json"});
  assert(json.code == 0);
  const auto document = nlohmann::json::parse(json.out);
  assert(document.at("format_version") == 1);
  assert(document.at("count") == 1);
  const auto &preset = document.at("presets").at(0);
  assert(preset.at("id") == PresetId);
  assert(preset.at("name") == "Test Ridge");
  assert(preset.at("label") == "Ridge");
  assert(preset.at("seed") == 4242);
  assert(preset.at("default_biome") == "rock");
  assert(preset.at("size").at(0) == 96);
  assert(preset.at("resolution").at(1) == 48);
  assert(preset.at("landform_count") == 3);
  assert(preset.at("biome_count") == 2);
  assert(preset.at("rule_count") == 2);
  assert(preset.at("default_landform") == "rock");
  assert(preset.at("rules").at(0) == "snowline");
}

void presetsReportAnEmptyProject(const std::filesystem::path &root) {
  const auto project = makeProject(root, "without_preset", PresetScene, false);
  const auto text = invoke({"terrain", "presets", project.string()});
  assert(text.code == 0);
  assert(text.error.empty());
  assert(contains(text.out, "(0)"));
  assert(contains(text.out, "terrain_preset"));

  const auto json =
      invoke({"terrain", "presets", project.string(), "--format", "json"});
  assert(json.code == 0);
  const auto document = nlohmann::json::parse(json.out);
  assert(document.at("count") == 0);
  assert(document.at("presets").empty());
}

void presetsOutputIsDeterministic(const std::filesystem::path &root) {
  const auto project = makeProject(root, "deterministic", PresetScene, true);
  const auto first = invoke({"terrain", "presets", project.string()});
  const auto second = invoke({"terrain", "presets", project.string()});
  assert(first.code == 0 && second.code == 0);
  assert(first.out == second.out);
  const auto firstJson =
      invoke({"terrain", "presets", project.string(), "--format", "json"});
  const auto secondJson =
      invoke({"terrain", "presets", project.string(), "--format", "json"});
  assert(firstJson.out == secondJson.out);
}

void presetsUsageErrors() {
  const auto missing = invoke({"terrain", "presets"});
  assert(missing.code == ExitUsageError);
  assert(contains(missing.error, "requires a project path"));

  const auto format =
      invoke({"terrain", "presets", "somewhere", "--format", "yaml"});
  assert(format.code == ExitUsageError);
  assert(contains(format.error, "--format must be text or json"));

  const auto unknown =
      invoke({"terrain", "presets", "somewhere", "--presets", "all"});
  assert(unknown.code == ExitUsageError);
  assert(contains(unknown.error, "Unknown option: --presets"));

  const auto absent = invoke({"terrain", "presets", "/nonexistent/project"});
  assert(absent.code == ExitValidationFailure);
  assert(contains(absent.error, "Project path does not exist"));
}

void applyPresetKeepsAuthoredStrokes(const std::filesystem::path &root) {
  const auto project = makeProject(root, "apply", PresetScene, true);
  const std::filesystem::path scenePath = project / "scenes/main.scene.json";
  const std::string before = readFile(scenePath);
  const auto run = invoke({"terrain", "apply-preset", project.string(),
                           "--preset", PresetId});
  assert(run.code == 0);
  assert(run.error.empty());
  assert(contains(run.out, std::string("Applied preset ") + PresetId));
  assert(contains(run.out, "to terrain in"));
  assert(contains(run.out, "Seed: 4242"));
  assert(contains(run.out, "Default biome: rock"));
  assert(contains(run.out, "Kept regions: 1, edits: 1"));

  const std::string after = readFile(scenePath);
  assert(after != before);
  const auto recipe = sceneRecipe(project);
  assert(recipe.has_value());
  assert(recipe->at("seed") == 4242);
  assert(recipe->at("default_biome") == "rock");
  assert(recipe->at("landforms").size() == 3);
  assert(recipe->at("biomes").size() == 2);
  assert(recipe->at("biomes").contains("snow"));
  assert(recipe->at("layers").size() == 5);
  assert(recipe->at("rules").size() == 2);
  assert(recipe->at("preset_id") == PresetId);
  assert(recipe->at("preset_version") == 1);
  // A preset supplies generation keys only, so painted and sculpted work
  // survives untouched.
  assert(recipe->at("regions").size() == 1);
  assert(recipe->at("regions").at(0).at("biome") == "rock");
  assert(recipe->at("edits").size() == 1);
  assert(recipe->at("edits").at(0).at("type") == "flatten");
  // The merged recipe must parse as a recipe, and the whole scene must still
  // pass the shared scene validator.
  demi::runtime::TerrainRecipe::parse(*recipe);
  const auto sceneDocument = nlohmann::json::parse(after);
  assert(!demi::hasErrors(demi::validateSceneDocument(scenePath, sceneDocument)));

  // The source-preserving patcher must have run: a whole-document dump(2)
  // rewrite would reformat the untouched source and reorder its keys.
  assert(contains(run.out, "a patched source diff"));
  assert(!contains(run.out, "a rewritten document"));
  assert(after != sceneDocument.dump(2) + "\n");

  // The patch stays inside the recipe: everything the command did not author
  // is byte-identical, and the file grows by a bounded amount instead of being
  // reformatted as a whole.
  const std::size_t terrainAt = before.find("\"Terrain3D\"");
  assert(terrainAt != std::string::npos);
  assert(after.find("\"Terrain3D\"") == terrainAt);
  assert(after.compare(0, terrainAt, before, 0, terrainAt) == 0);
  const std::vector<std::string> beforeLines = textLines(before);
  const std::vector<std::string> afterLines = textLines(after);
  assert(afterLines.size() > beforeLines.size());
  assert(afterLines.size() <= beforeLines.size() + 64);
}

void applyPresetReportsItsWriteMode(const std::filesystem::path &root) {
  const auto project = makeProject(root, "apply_json", PresetScene, true);
  const auto run = invoke({"terrain", "apply-preset", project.string(),
                           "--preset", PresetId, "--format", "json"});
  assert(run.code == 0);
  const auto document = nlohmann::json::parse(run.out);
  assert(document.at("format_version") == 1);
  assert(document.at("entity") == "terrain");
  assert(document.at("preset_id") == PresetId);
  assert(document.at("preset_version") == 1);
  assert(document.at("write") == "patched");
  assert(document.at("recipe").at("preset_id") == PresetId);
  assert(document.at("recipe").at("regions").size() == 1);
}

void applyPresetSelectsOneOfSeveralTerrains(
    const std::filesystem::path &root) {
  const std::string second =
      R"(,
    {
      "id": "terrain_two",
      "name": "Second terrain",
      "components": {
        "Terrain3D": {
          "recipe": {
            "format_version": 1,
            "size": [
              32,
              32
            ],
            "resolution": [
              16,
              16
            ],
            "seed": 7,
            "default_biome": "rock",
            "default_landform": "rock",
            "biomes": {
              "rock": {
                "landform": "rock",
                "color": [
                  0.5,
                  0.5,
                  0.5,
                  1
                ]
              }
            },
            "landforms": {
              "default": {
                "base_height": 4.0,
                "height_variation": 3.0
              },
              "rock": {
                "base_height": 4.0,
                "height_variation": 3.0
              }
            }
          }
        }
      }
    })";
  std::string scene = PresetScene;
  const std::size_t end = scene.rfind("  ]\n}");
  assert(end != std::string::npos);
  scene = scene.substr(0, end) + second + "\n" + scene.substr(end);
  const auto project = makeProject(root, "several", scene, true);

  const auto ambiguous = invoke(
      {"terrain", "apply-preset", project.string(), "--preset", PresetId});
  assert(ambiguous.code == ExitUsageError);
  assert(contains(ambiguous.error, "2 Terrain3D entities"));
  assert(contains(ambiguous.error, "terrain, terrain_two"));
  assert(contains(ambiguous.error, "--entity"));

  const auto selected = invoke({"terrain", "apply-preset", project.string(),
                                "--preset", PresetId, "--entity",
                                "terrain_two"});
  assert(selected.code == 0);
  const auto recipe = sceneRecipe(project, "terrain_two");
  assert(recipe.has_value());
  assert(recipe->at("preset_id") == PresetId);
  // The terrain that was not selected keeps its authored seed.
  const auto untouched = sceneRecipe(project, "terrain");
  assert(untouched.has_value());
  assert(untouched->at("seed") == 1337);
}

void applyPresetFailuresAreDistinct(const std::filesystem::path &root) {
  const auto project = makeProject(root, "failures", PresetScene, true);
  const std::filesystem::path scenePath = project / "scenes/main.scene.json";

  const auto missingPreset = invoke({"terrain", "apply-preset", project.string(),
                                     "--preset",
                                     "asset://terrain/presets/none"});
  assert(missingPreset.code == ExitValidationFailure);
  assert(missingPreset.out.empty());
  assert(contains(missingPreset.error, "Terrain preset was not found"));

  const auto wrongType = invoke({"terrain", "apply-preset", project.string(),
                                 "--preset", PresetSchemaId});
  assert(wrongType.code == ExitValidationFailure);
  assert(contains(wrongType.error, "must be a DataAsset"));

  const auto badEntity =
      invoke({"terrain", "apply-preset", project.string(), "--preset",
              PresetId, "--entity", "marker"});
  assert(badEntity.code == ExitValidationFailure);
  assert(contains(badEntity.error, "has no Terrain3D component"));
  assert(contains(badEntity.error, "Terrain entities: terrain"));

  const auto absentEntity =
      invoke({"terrain", "apply-preset", project.string(), "--preset",
              PresetId, "--entity", "absent"});
  assert(absentEntity.code == ExitValidationFailure);
  assert(contains(absentEntity.error, "has no Terrain3D component"));

  const auto noPresetFlag =
      invoke({"terrain", "apply-preset", project.string()});
  assert(noPresetFlag.code == ExitUsageError);
  assert(contains(noPresetFlag.error, "--preset"));

  const auto noProject =
      invoke({"terrain", "apply-preset", "--preset", PresetId});
  assert(noProject.code == ExitUsageError);
  assert(contains(noProject.error, "requires a project path"));

  const std::string original = readFile(scenePath);
  assert(contains(original, "\"seed\": 1337"));
  assert(!contains(original, "preset_id"));

  const auto bare = makeProject(root, "bare", R"({
  "format_version": 1,
  "id": "scene://test/main",
  "name": "Bare",
  "entities": [{"id": "marker", "name": "Marker"}]
})",
                                 true);
  const auto noTerrainEntity =
      invoke({"terrain", "apply-preset", bare.string(), "--preset", PresetId});
  assert(noTerrainEntity.code == ExitValidationFailure);
  assert(contains(noTerrainEntity.error, "No Terrain3D entity"));

  const auto malformed = root / "malformed_project";
  writeFile(malformed, "demi.project.json", "{ this is not json");
  const auto brokenProject = invoke(
      {"terrain", "apply-preset", malformed.string(), "--preset", PresetId});
  assert(brokenProject.code == ExitValidationFailure);
  assert(contains(brokenProject.error, "Invalid project"));

  const auto incomplete = root / "incomplete_project";
  writeFile(incomplete, "demi.project.json",
            R"({"format_version": 1, "name": "Incomplete"})");
  const auto missingScenes = invoke(
      {"terrain", "apply-preset", incomplete.string(), "--preset", PresetId});
  assert(missingScenes.code == ExitValidationFailure);
  assert(contains(missingScenes.error, "Invalid project"));
  assert(contains(missingScenes.error, "main_scene"));

  // Every failure above is a no-write, so the authored source is untouched.
  assert(readFile(scenePath) == original);
}

void applyPresetLeavesNoPartialWrite(const std::filesystem::path &root) {
  // A region painted with a biome the preset does not define is a merge the
  // engine must reject: strokes are kept verbatim, so the merged recipe no
  // longer names that biome.
  std::string scene = PresetScene;
  const std::string painted = "\"biome\": \"rock\"";
  const auto paintedAt = scene.find(painted);
  assert(paintedAt != std::string::npos);
  scene.replace(paintedAt, painted.size(), "\"biome\": \"meadow\"");
  const auto project = makeProject(root, "merge_failure", scene, true);
  const std::filesystem::path scenePath = project / "scenes/main.scene.json";
  const std::string original = readFile(scenePath);

  const auto run =
      invoke({"terrain", "apply-preset", project.string(), "--preset", PresetId});
  assert(run.code == ExitValidationFailure);
  assert(run.out.empty());
  assert(contains(run.error, "Failed to apply preset "));
  assert(contains(run.error, "unknown biome"));
  assert(readFile(scenePath) == original);
  assert(!contains(readFile(scenePath), "preset_id"));
}

void compactPreservesOperations(const std::filesystem::path &root) {
  auto recipe = demi::runtime::TerrainRecipe::defaults();
  recipe["edits"] = nlohmann::json::array();
  for (int index = 0; index < 80; ++index)
    recipe["edits"].push_back({{"type", "flatten"},
                                {"center", {index * .5F, 8}},
                                {"radius", 8},
                                {"target_height", 12},
                                {"strength", .4F}});
  nlohmann::json source{{"format_version", 1},
                         {"id", "asset://terrain/test"},
                         {"name", "Keep this name"},
                         {"recipe", recipe}};
  const auto path = writeRecipe(root, "compact.terrain.json", source.dump(2));
  const auto original = readFile(path);
  const auto preview = invoke({"terrain", "compact", path});
  assert(preview.code == 0 && readFile(path) == original);
  const auto written = invoke({"terrain", "compact", path, "--write"});
  assert(written.code == 0);
  const auto compact = readFile(path);
  assert(compact.size() < original.size());
  const auto parsed = nlohmann::json::parse(compact);
  assert(parsed.at("name") == source.at("name"));
  assert(parsed.at("recipe").at("edits").size() == 1);
  assert(demi::runtime::TerrainRecipe::parse(parsed.at("recipe")).toJson() ==
         demi::runtime::TerrainRecipe::parse(recipe).toJson());
  assert(invoke({"terrain", "compact", path, "--write"}).code == 0);
  assert(readFile(path) == compact);
}
} // namespace

int main() {
  const auto root = testRoot();
  inspectTextAndJson(root);
  inspectReportsRuleConditions(root);
  inspectRejectsInvalidRecipe(root);
  explainWithoutRules(root);
  explainNamesTheRuleThatAssignedTheSample(root);
  explainReportsPaintedRegionOverride(root);
  explainAtBoundsClampToLastCell(root);
  explainAtRejectsBadInput(root);
  seedsTextAndJson();
  usageErrors();
  sceneIsNotARecipe(root);
  outputIsDeterministic(root);
  presetsListProjectPresets(root);
  presetsReportAnEmptyProject(root);
  presetsOutputIsDeterministic(root);
  presetsUsageErrors();
  applyPresetKeepsAuthoredStrokes(root);
  applyPresetReportsItsWriteMode(root);
  applyPresetSelectsOneOfSeveralTerrains(root);
  applyPresetFailuresAreDistinct(root);
  applyPresetLeavesNoPartialWrite(root);
  compactPreservesOperations(root);
  std::filesystem::remove_all(root);
  std::cout << "Terrain CLI checks passed\n";
}
