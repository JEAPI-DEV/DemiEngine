#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/runtime/terrain/TerrainPalette.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace demi;
using namespace demi::runtime;

namespace {

constexpr std::string_view PaletteId = "asset://terrain/palettes/test";
constexpr std::string_view SchemaId = "asset://schemas/terrain_palette";
constexpr std::string_view GrassAsset = "asset://surfaces/grass";
constexpr std::string_view RockAsset = "asset://surfaces/rock";
constexpr std::string_view PrototypeMesh =
    "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";

std::filesystem::path root;
AssetRegistry registry;

bool write(const std::filesystem::path &path, const std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  return output.good();
}

AssetManifest manifest(const std::string &id, const std::string &type,
                       const std::string &source,
                       std::string settings = "{}",
                       std::vector<std::string> dependencies = {}) {
  return {.id = id,
          .type = type,
          .importer = type == "DataSchema" ? "json_schema" : "json_data",
          .importerVersion = 1,
          .sourceHash = "hash:1",
          .dependencies = std::move(dependencies),
          .settingsJson = std::move(settings),
          .manifestPath = root / (source + ".asset.json"),
          .sourcePath = root / source,
          .sourcePaths = {root / source}};
}

// One registry serves every case: the palette document is rewritten between
// loads, so a rejection proves the loader read the document under test.
AssetRegistry buildRegistry() {
  AssetRegistry built;
  built.projectDirectory = root;
  built.assets = {
      manifest(std::string(GrassAsset), "Model3D", "grass.obj",
               R"({"content_type":"surface"})"),
      manifest(std::string(RockAsset), "Model3D", "rock.obj",
               R"({"content_type":"surface"})"),
      manifest("asset://surfaces/material_only", "Material", "grass.material.json"),
      manifest(std::string(SchemaId), "DataSchema", "schema.json"),
      manifest("asset://data/notes", "DataAsset", "notes.json",
               R"({"content_type":"item","tags":[]})"),
      manifest(std::string(PaletteId), "DataAsset", "palette.json",
               R"({"content_type":"terrain_palette","schema":")" +
                   std::string(SchemaId) + R"(","tags":[]})",
               {std::string(SchemaId), std::string(GrassAsset),
                std::string(RockAsset)})};
  std::ranges::sort(built.assets, {}, &AssetManifest::id);
  return built;
}

void writePalette(const std::string_view document) {
  assert(write(root / "palette.json", document));
}

// Returns the rejection message, or an empty string when the document loaded.
std::string rejection(const std::string_view document) {
  writePalette(document);
  try {
    (void)loadTerrainPalette(registry, PaletteId);
  } catch (const std::invalid_argument &failure) {
    return failure.what();
  }
  return {};
}

void expectRejected(const std::string_view document,
                    const std::string_view expected) {
  const std::string message = rejection(document);
  if (message.empty() || message.find(expected) == std::string::npos)
    std::cerr << "expected a rejection containing \"" << expected
              << "\" but got \"" << message << "\"\n";
  assert(!message.empty());
  assert(message.find(expected) != std::string::npos);
}

std::optional<TerrainPalette> load(const std::string_view document) {
  writePalette(document);
  return loadTerrainPalette(registry, PaletteId);
}

std::string describe(const TerrainPalette &palette) {
  std::string text = std::to_string(palette.formatVersion) + "|" + palette.id +
                     "|" + palette.name;
  for (const auto &[role, entry] : palette.roles) {
    text += "|" + role + "=" + entry.asset + "," + entry.prefab + "," +
            std::to_string(entry.weight) + "," +
            std::to_string(entry.scaleMin) + "," +
            std::to_string(entry.scaleMax) + "," +
            std::to_string(entry.spacing) + "," +
            std::string(terrainCollisionPolicyName(entry.collision)) + "," +
            std::to_string(entry.lod) + "," +
            std::string(terrainPaletteRoleName(entry.role));
  }
  for (const std::string &required : palette.requiredRoles)
    text += "|" + required;
  return text;
}

constexpr std::string_view Minimal =
    R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass"}}})json";

constexpr std::string_view Full = R"json({
  "format_version": 1,
  "name": "Meadow shoreline",
  "roles": {
    "exposed_rock": {"asset": "asset://surfaces/rock", "weight": 0.3, "scale": [0.8, 2.4], "spacing": 6, "collision": "static", "lod": 1},
    "soil": {"asset": "asset://surfaces/soil", "weight": 1, "scale": [1, 1], "spacing": 0, "collision": "none", "lod": 0},
    "sand": {"asset": "asset://surfaces/sand", "weight": 0.8, "scale": [1, 1], "spacing": 0, "collision": "none", "lod": 0},
    "snow": {"asset": "asset://surfaces/snow", "weight": 0.5, "scale": [1, 1], "spacing": 0, "collision": "none", "lod": 0},
    "wet_ground": {"asset": "asset://surfaces/wet_ground", "weight": 0.6, "scale": [1, 1], "spacing": 0, "collision": "none", "lod": 0},
    "tree": {"asset": "asset://surfaces/tree", "weight": 0.15, "scale": [0.9, 1.3], "spacing": 12, "collision": "static", "lod": 2},
    "bush": {"asset": "asset://surfaces/bush", "weight": 0.5, "scale": [0.7, 1.4], "spacing": 3, "collision": "trigger", "lod": 1},
    "grass": {"asset": "asset://surfaces/grass", "weight": 1.5, "scale": [0.8, 1.2], "spacing": 0.5, "collision": "none", "lod": 0},
    "reed": {"asset": "asset://surfaces/reed", "weight": 0.7, "scale": [0.85, 1.15], "spacing": 0.75, "collision": "none", "lod": 0},
    "cliff_piece": {"asset": "asset://surfaces/cliff_piece", "weight": 0.25, "scale": [1, 2.2], "spacing": 8, "collision": "static", "lod": 2},
    "debris": {"asset": "asset://surfaces/debris", "weight": 0.35, "scale": [0.5, 1.1], "spacing": 2, "collision": "trigger", "lod": 1}
  },
  "required_roles": ["debris", "grass", "exposed_rock", "grass"]
})json";

bool containsCode(const Diagnostics &diagnostics, const std::string_view code) {
  return std::ranges::any_of(diagnostics, [&](const Diagnostic &item) {
    return item.code == code;
  });
}

bool containsMessage(const Diagnostics &diagnostics,
                     const std::string_view needle) {
  return std::ranges::any_of(diagnostics, [&](const Diagnostic &item) {
    return item.message.find(needle) != std::string::npos;
  });
}

void validPaletteLoads() {
  const auto minimal = load(Minimal);
  assert(minimal.has_value());
  assert(minimal->formatVersion == 1);
  assert(minimal->id == PaletteId);
  assert(minimal->name == "Test");
  assert(minimal->roles.size() == 1);
  // Omitted fields keep their documented defaults instead of being
  // materialized, so a minimal entry stays minimal.
  const auto &entry = minimal->roles.at("grass");
  assert(entry.role == TerrainPaletteRole::Grass);
  assert(entry.asset == GrassAsset);
  assert(entry.prefab.empty());
  assert(entry.weight == 1.F);
  assert(entry.scaleMin == 1.F && entry.scaleMax == 1.F);
  assert(entry.spacing == 1.F);
  assert(entry.collision == TerrainCollisionPolicy::Static);
  assert(entry.lod == 0);
  assert(minimal->requiredRoles.empty());

  writePalette(Full);
  AssetRegistry full = registry;
  // The full document names eleven surfaces, so give the registry each one.
  for (const TerrainPaletteRole role :
       {TerrainPaletteRole::ExposedRock, TerrainPaletteRole::Soil,
        TerrainPaletteRole::Sand, TerrainPaletteRole::Snow,
        TerrainPaletteRole::WetGround, TerrainPaletteRole::Tree,
        TerrainPaletteRole::Bush, TerrainPaletteRole::Grass,
        TerrainPaletteRole::Reed, TerrainPaletteRole::CliffPiece,
        TerrainPaletteRole::Debris}) {
    const std::string name(terrainPaletteRoleName(role));
    const std::string id = "asset://surfaces/" + name;
    assert(write(root / (name + ".obj"), PrototypeMesh));
    full.assets.push_back(manifest(id, "Model3D", name + ".obj"));
  }
  std::ranges::sort(full.assets, {}, &AssetManifest::id);
  const auto loaded = loadTerrainPalette(full, PaletteId);
  assert(loaded.has_value());
  assert(loaded->roles.size() == 11);
  assert(loaded->name == "Meadow shoreline");
  const auto &rock = loaded->roles.at("exposed_rock");
  assert(rock.role == TerrainPaletteRole::ExposedRock);
  assert(rock.asset == "asset://surfaces/rock");
  assert(rock.weight == 0.3F);
  assert(rock.scaleMin == 0.8F && rock.scaleMax == 2.4F);
  assert(rock.spacing == 6.F);
  assert(rock.collision == TerrainCollisionPolicy::Static);
  assert(rock.lod == 1);
  const auto &tree = loaded->roles.at("tree");
  assert(tree.collision == TerrainCollisionPolicy::Static && tree.lod == 2);
  assert(loaded->roles.at("bush").collision == TerrainCollisionPolicy::Trigger);
  assert(loaded->roles.at("soil").spacing == 0.F);
  // required_roles is normalized, so a duplicated authored entry collapses.
  assert((loaded->requiredRoles ==
          std::vector<std::string>{"debris", "exposed_rock", "grass"}));
}

void roleVocabularyRoundTrips() {
  constexpr TerrainPaletteRole roles[]{
      TerrainPaletteRole::ExposedRock, TerrainPaletteRole::Soil,
      TerrainPaletteRole::Sand,       TerrainPaletteRole::Snow,
      TerrainPaletteRole::WetGround,  TerrainPaletteRole::Tree,
      TerrainPaletteRole::Bush,       TerrainPaletteRole::Grass,
      TerrainPaletteRole::Reed,       TerrainPaletteRole::CliffPiece,
      TerrainPaletteRole::Debris};
  std::set<std::string_view, std::less<>> names;
  for (const TerrainPaletteRole role : roles) {
    const std::string_view name = terrainPaletteRoleName(role);
    assert(!name.empty());
    assert(names.insert(name).second);
    const auto parsed = terrainPaletteRoleFromName(name);
    assert(parsed.has_value());
    assert(*parsed == role);
  }
  assert(names.size() == 11);
  assert(!terrainPaletteRoleFromName("moss").has_value());
  assert(!terrainPaletteRoleFromName("").has_value());
  // The documented spellings are stable identity, not presentation.
  assert(terrainPaletteRoleName(TerrainPaletteRole::ExposedRock) ==
         "exposed_rock");
  assert(terrainPaletteRoleName(TerrainPaletteRole::CliffPiece) ==
         "cliff_piece");
  assert(terrainPaletteRoleName(TerrainPaletteRole::WetGround) ==
         "wet_ground");
}

void collisionPolicyRoundTrips() {
  assert(terrainCollisionPolicyName(TerrainCollisionPolicy::None) == "none");
  assert(terrainCollisionPolicyName(TerrainCollisionPolicy::Static) ==
         "static");
  assert(terrainCollisionPolicyName(TerrainCollisionPolicy::Trigger) ==
         "trigger");
  const std::string prefix =
      R"({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","collision":")";
  const std::string suffix = R"("}}})";
  for (const TerrainCollisionPolicy policy :
       {TerrainCollisionPolicy::None, TerrainCollisionPolicy::Static,
        TerrainCollisionPolicy::Trigger}) {
    const std::string document =
        prefix + std::string(terrainCollisionPolicyName(policy)) + suffix;
    const auto loaded = load(document);
    assert(loaded.has_value());
    assert(loaded->roles.at("grass").collision == policy);
  }
}

// The DataAsset layer rejects a wrong format_version before the palette rules
// run, so the palette's own header rules are exercised through the document
// entry point a Data.load consumer uses.
void rejectsInvalidDocumentHeader() {
  auto rejectionMessage = [](const std::string_view document) -> std::string {
    const assets::DataDocumentResult parsed =
        assets::parseDataDocument(document);
    assert(parsed.document != nullptr);
    try {
      (void)parseTerrainPalette(*parsed.document, registry, PaletteId);
    } catch (const std::invalid_argument &failure) {
      return std::string(failure.what());
    }
    return std::string();
  };
  const std::string badVersion = rejectionMessage(
      R"json({"format_version":2,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass"}}})json");
  if (badVersion.find("format_version: must be 1") == std::string::npos)
    std::cerr << "wrong format_version message: " << badVersion << "\n";
  assert(badVersion.find("format_version: must be 1") != std::string::npos);
  const std::vector<std::string> otherHeaders{
      R"json({"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass"}}})json",
      R"json({"format_version":"1","name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass"}}})json"};
  for (const std::string &document : otherHeaders) {
    const std::string message = rejectionMessage(document);
    if (message.empty() || message.find("format_version") == std::string::npos)
      std::cerr << "wrong format_version message: " << message << "\n";
    assert(!message.empty());
    assert(message.find("format_version") != std::string::npos);
  }
  const std::string notObject = rejectionMessage(R"([1,2,3])");
  if (notObject.find("document: must be an object") == std::string::npos)
    std::cerr << "wrong root message: " << notObject << "\n";
  assert(notObject.find("document: must be an object") != std::string::npos);
  // A document that does pass the palette rules parses without a manifest.
  const assets::DataDocumentResult parsed =
      assets::parseDataDocument(Minimal);
  const TerrainPalette palette =
      parseTerrainPalette(*parsed.document, registry, PaletteId);
  assert(palette.id == PaletteId);
  assert(palette.roles.size() == 1);
}

void rejectsUnknownRole() {
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"moss":{"asset":"asset://surfaces/grass"}}})json",
      "roles/moss: is not a known role");
  // The message has to name the vocabulary, otherwise a typo stays a typo.
  assert(rejection(
             R"json({"format_version":1,"name":"Test","roles":{"moss":{"asset":"asset://surfaces/grass"}}})json")
             .find("cliff_piece") != std::string::npos);
}

void rejectsDocumentShape() {
  expectRejected(R"json({"format_version":1,"roles":{"grass":{"asset":"asset://surfaces/grass"}}})json",
                 "name: is required");
  expectRejected(R"json({"format_version":1,"name":"","roles":{"grass":{"asset":"asset://surfaces/grass"}}})json",
                 "name: must be a non-empty string");
  expectRejected(R"json({"format_version":1,"name":"Test"})json",
                 "roles: is required");
  expectRejected(R"json({"format_version":1,"name":"Test","roles":{}})json",
                 "roles: must be a non-empty object keyed by role name");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":[{"asset":"asset://surfaces/grass"}]})json",
      "roles: must be a non-empty object keyed by role name");
  // A misspelled top-level key would otherwise be silently ignored.
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass"}},"require_roles":["grass"]})json",
      "document: has unknown field \"require_roles\"");
}

void rejectsEntryShape() {
  expectRejected(R"json({"format_version":1,"name":"Test","roles":{"grass":{}}})json",
                 "roles/grass/asset: is required");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","weigh":2}}})json",
      "roles/grass: has unknown field \"weigh\"");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":""}}})json",
      "roles/grass: asset must not be empty");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"surfaces/grass"}}})json",
      "asset must be an asset:// reference");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":7}}})json",
      "roles/grass/asset: must be a string");
}

void rejectsUnresolvableAsset() {
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/absent"}}})json",
      "asset does not resolve: asset://surfaces/absent");
}

void rejectsInvalidPrefab() {
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/material_only"}}})json",
      "must be a Model3D when no prefab supplies geometry");
  const auto materialWithGeometry = load(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/material_only","prefab":"prefab://nature/oak"}}})json");
  assert(materialWithGeometry.has_value());
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","prefab":""}}})json",
      "roles/grass/prefab: must not be empty");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","prefab":"asset://surfaces/grass"}}})json",
      "must be a prefab:// reference");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","prefab":3}}})json",
      "roles/grass/prefab: must be a string");
  // A well-formed prefab reference is optional metadata, not a registry lookup.
  const auto loaded = load(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","prefab":"prefab://nature/oak"}}})json");
  assert(loaded.has_value());
  assert(loaded->roles.at("grass").prefab == "prefab://nature/oak");
}

void rejectsInvalidRanges() {
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","weight":-1}}})json",
      "roles/grass/weight: must be at least 0");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","weight":"heavy"}}})json",
      "roles/grass/weight: must be a finite number");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","scale":[1]}}})json",
      "roles/grass/scale: must be [scale_min, scale_max]");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","scale":[0,1]}}})json",
      "scale_min must be greater than 0");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","scale":[2,1]}}})json",
      "scale_min must not exceed scale_max");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","spacing":-0.5}}})json",
      "roles/grass/spacing: must be at least 0 world units");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","lod":-1}}})json",
      "roles/grass/lod: must be at least 0");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","lod":1.5}}})json",
      "roles/grass/lod: must be an integer");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","collision":"ghost"}}})json",
      "has unknown collision policy \"ghost\"");
  // Zero spacing and zero weight are legal values, not missing ones, as long
  // as some other role can still be selected.
  const auto zeroed = load(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","weight":0,"spacing":0,"scale":[1,1]},"soil":{"asset":"asset://surfaces/rock","weight":1}}})json");
  assert(zeroed.has_value());
  assert(zeroed->roles.at("grass").weight == 0.F);
  assert(zeroed->roles.at("grass").spacing == 0.F);
}

void rejectsUnselectablePalette() {
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","weight":0}}})json",
      "every entry has weight 0");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","weight":0},"soil":{"asset":"asset://surfaces/rock","weight":0}}})json",
      "every entry has weight 0");
  // One selectable role is enough.
  assert(load(
             R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","weight":0},"soil":{"asset":"asset://surfaces/rock","weight":0.5}}})json")
             .has_value());
}

void rejectsConflictingSharedAsset() {
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","weight":1},"soil":{"asset":"asset://surfaces/grass","weight":2}}})json",
      "reuses asset asset://surfaces/grass already claimed by roles/grass");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","scale":[1,1]},"soil":{"asset":"asset://surfaces/grass","scale":[1,2]}}})json",
      "with different placement metadata");
  // Sharing an asset is fine when both roles place it identically, because
  // only one metadata set has to be honoured.
  const auto shared = load(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass","weight":1,"spacing":2},"soil":{"asset":"asset://surfaces/grass","weight":1,"spacing":2}}})json");
  assert(shared.has_value());
  assert(shared->roles.size() == 2);
  assert(shared->assetDependencies() ==
         std::vector<std::string>{"asset://surfaces/grass"});
}

void rejectsInvalidRequiredRoles() {
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass"}},"required_roles":{}})json",
      "required_roles: must be an array of role names");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass"}},"required_roles":["moss"]})json",
      "required_roles[0]: is not a known role");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass"}},"required_roles":["tree"]})json",
      "requires role \"tree\", which the palette does not define");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass"}},"required_roles":[3]})json",
      "required_roles[0]: must be a string");
  const auto loaded = load(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass"},"soil":{"asset":"asset://surfaces/rock"}},"required_roles":["soil","grass"]})json");
  assert(loaded.has_value());
  assert((loaded->requiredRoles == std::vector<std::string>{"grass", "soil"}));
  assert(load(Minimal)->requiredRoles.empty());
}

void rejectsWrongManifestKind() {
  writePalette(Minimal);
  try {
    (void)loadTerrainPalette(registry, "asset://surfaces/grass");
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(std::string(failure.what())
               .find("must be a DataAsset: asset://surfaces/grass is Model3D") !=
           std::string::npos);
  }
  try {
    (void)loadTerrainPalette(registry, "asset://surfaces/absent");
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(std::string(failure.what())
               .find("was not found: asset://surfaces/absent") !=
           std::string::npos);
  }
  // A DataAsset with a different content type is a different document kind.
  try {
    (void)loadTerrainPalette(registry, "asset://data/notes");
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(std::string(failure.what())
               .find("must declare settings.content_type \"terrain_palette\"") !=
           std::string::npos);
  }
  try {
    (void)loadTerrainPalette(registry, "asset://schemas/terrain_palette");
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(std::string(failure.what())
               .find("must be a DataAsset: asset://schemas/terrain_palette is "
                     "DataSchema") != std::string::npos);
  }
}

void rejectsUndeclaredDependency() {
  writePalette(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/grass"}}})json");
  assert(!hasErrors(assets::validateDataAssets(registry)));
  // The loader does not read manifest dependencies, so declaring them is the
  // DataAsset layer's contract and it has to fail a palette that omits one.
  AssetRegistry undeclared = registry;
  for (AssetManifest &asset : undeclared.assets)
    if (asset.id == PaletteId)
      asset.dependencies = {std::string(SchemaId)};
  const Diagnostics diagnostics = assets::validateDataAssets(undeclared);
  assert(containsCode(diagnostics, "DATA_DEPENDENCY_UNDECLARED"));
  assert(containsMessage(diagnostics, "asset://surfaces/grass"));
  // Dropping the schema declaration fails the same way, with its own message.
  AssetRegistry unschemaed = registry;
  for (AssetManifest &asset : unschemaed.assets)
    if (asset.id == PaletteId)
      asset.dependencies.clear();
  assert(containsCode(assets::validateDataAssets(unschemaed),
                      "DATA_DEPENDENCY_UNDECLARED"));
}

void dependenciesAreSortedAndDeduped() {
  const auto loaded = load(
      R"json({"format_version":1,"name":"Test","roles":{"grass":{"asset":"asset://surfaces/rock"},"soil":{"asset":"asset://surfaces/rock"},"tree":{"asset":"asset://surfaces/grass","prefab":"prefab://nature/oak"},"bush":{"asset":"asset://surfaces/grass","prefab":"prefab://nature/oak"}}})json");
  assert(loaded.has_value());
  const std::vector<std::string> expected{"asset://surfaces/grass",
                                          "asset://surfaces/rock",
                                          "prefab://nature/oak"};
  assert(loaded->assetDependencies() == expected);
  // Repeating the query cannot reorder or duplicate anything.
  assert(loaded->assetDependencies() == loaded->assetDependencies());
  assert(loaded->assetDependencies().size() == 3);
}

void loadingIsDeterministic() {
  const std::string first = describe(*load(Minimal));
  const std::string second = describe(*load(Minimal));
  assert(first == second);
  // A role map is key-sorted, so a differently ordered document is identical.
  const std::string reordered = describe(*load(
      R"json({"roles":{"soil":{"asset":"asset://surfaces/rock"}},"name":"Test","format_version":1})json"));
  assert(reordered == describe(*load(
      R"json({"format_version":1,"name":"Test","roles":{"soil":{"asset":"asset://surfaces/rock"}}})json")));
}

void examplePaletteLoadsFromDisk() {
  const AssetRegistry example =
      loadAuthoredAssetRegistry(DEMI_SOURCE_DIR "/examples/terrain_3d");
  const std::optional<TerrainPalette> palette =
      loadTerrainPalette(example, "asset://terrain/palettes/meadow");
  assert(palette.has_value());
  assert(palette->formatVersion == 1);
  assert(palette->name == "Meadow shoreline");
  assert(palette->roles.size() == 11);
  for (const TerrainPaletteRole role :
       {TerrainPaletteRole::ExposedRock, TerrainPaletteRole::Soil,
        TerrainPaletteRole::Sand, TerrainPaletteRole::Snow,
        TerrainPaletteRole::WetGround, TerrainPaletteRole::Tree,
        TerrainPaletteRole::Bush, TerrainPaletteRole::Grass,
        TerrainPaletteRole::Reed, TerrainPaletteRole::CliffPiece,
        TerrainPaletteRole::Debris})
    assert(palette->roles.contains(terrainPaletteRoleName(role)));
  assert((palette->requiredRoles ==
          std::vector<std::string>{"exposed_rock", "grass", "soil", "tree"}));
  const std::vector<std::string> dependencies = palette->assetDependencies();
  assert(dependencies.size() == 12);
  assert(std::is_sorted(dependencies.begin(), dependencies.end()));
  assert(std::ranges::count_if(dependencies, [](const std::string &id) {
    return id.starts_with("asset://");
  }) == 11);
  assert(std::ranges::find(dependencies, "prefab://scatter_prototype") !=
         dependencies.end());
  assert(std::ranges::none_of(dependencies, [](const std::string &id) {
    return id == "asset://schemas/terrain_palette";
  }));
  // Cook follows the manifest, so every reference the document makes has to be
  // declared there or the palette ships without its art.
  const AssetManifest *declared = findAsset(example, palette->id);
  assert(declared != nullptr);
  for (const std::string &dependency : dependencies)
    if (dependency.starts_with("asset://"))
      assert(std::ranges::find(declared->dependencies, dependency) !=
             declared->dependencies.end());
  assert(palette->roles.at("tree").collision == TerrainCollisionPolicy::Static);
  assert(palette->roles.at("debris").collision ==
         TerrainCollisionPolicy::Trigger);
  assert(palette->roles.at("soil").spacing == 0.F);
  // The checked-in example must also satisfy the DataAsset schema contract.
  assert(!hasErrors(assets::validateDataAssets(example)));
}

} // namespace

int main() {
  root = std::filesystem::temp_directory_path() / "demi_terrain_palette_tests";
  std::error_code filesystemError;
  std::filesystem::remove_all(root, filesystemError);
  std::filesystem::create_directories(root, filesystemError);
  if (!write(root / "grass.material.json",
             R"({"format_version":1,"shader":"builtin://lit"})") ||
      !write(root / "grass.obj", PrototypeMesh) ||
      !write(root / "rock.obj", PrototypeMesh) ||
      !write(root / "rock.material.json",
             R"({"format_version":1,"shader":"builtin://lit"})") ||
      !write(root / "notes.json", R"({"format_version":1,"note":"x"})") ||
      !write(root / "schema.json", R"({
        "format_version": 1,
        "type": "object",
        "required": ["format_version", "name", "roles"],
        "properties": {
          "format_version": {"type": "integer", "enum": [1]},
          "name": {"type": "string"},
          "roles": {
            "type": "object",
            "properties": {
              "grass": {
                "type": "object",
                "required": ["asset"],
                "properties": {
                  "asset": {"type": "string", "reference": "asset"},
                  "weight": {"type": "number", "minimum": 0}
                }
              }
            }
          },
          "required_roles": {"type": "array", "items": {"type": "string"}}
        }
      })")) {
    std::cerr << "Could not create terrain palette fixtures.\n";
    return 1;
  }
  registry = buildRegistry();
  writePalette(Minimal);

  validPaletteLoads();
  roleVocabularyRoundTrips();
  collisionPolicyRoundTrips();
  rejectsInvalidDocumentHeader();
  rejectsUnknownRole();
  rejectsDocumentShape();
  rejectsEntryShape();
  rejectsUnresolvableAsset();
  rejectsInvalidPrefab();
  rejectsInvalidRanges();
  rejectsUnselectablePalette();
  rejectsConflictingSharedAsset();
  rejectsInvalidRequiredRoles();
  rejectsWrongManifestKind();
  rejectsUndeclaredDependency();
  dependenciesAreSortedAndDeduped();
  loadingIsDeterministic();
  examplePaletteLoadsFromDisk();

  std::filesystem::remove_all(root, filesystemError);
  std::cout << "Terrain palette checks passed\n";
  return 0;
}
