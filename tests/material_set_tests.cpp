#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/DataDocument.h"
#include "demi/assets/MaterialAsset.h"
#include "demi/assets/MaterialSet.h"

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
using namespace demi::assets;

namespace {

constexpr std::string_view SetId = "asset://materials/pbr_set";
constexpr std::string_view SchemaId = "asset://schemas/terrain_material_set";
constexpr std::string_view RenderMaterialId = "asset://materials/stone";
constexpr std::string_view TerrainMaterialId = "asset://materials/clay";
constexpr std::string_view BrokenMaterialId = "asset://materials/broken";
constexpr std::string_view AudioId = "asset://audio/wind";
constexpr std::string_view NotesId = "asset://data/notes";

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
          .importer = type == "DataSchema"
                          ? "json_schema"
                          : (type == "DataAsset" ? "json_data" : type),
          .importerVersion = 1,
          .sourceHash = "hash:1",
          .dependencies = std::move(dependencies),
          .settingsJson = std::move(settings),
          .manifestPath = root / (source + ".asset.json"),
          .sourcePath = root / source,
          .sourcePaths = {root / source}};
}

// One registry serves every case: the set document is rewritten between loads,
// so a rejection proves the loader read the document under test.
AssetRegistry buildRegistry() {
  AssetRegistry built;
  built.projectDirectory = root;
  built.assets = {
      manifest(std::string(SchemaId), "DataSchema", "schema.json"),
      manifest(std::string(NotesId), "DataAsset", "notes.json",
               R"({"content_type":"item","tags":[]})"),
      // Both material carriers are legal role targets: a render Material and a
      // terrain_material DataAsset.
      manifest(std::string(RenderMaterialId), "Material", "stone.material.json"),
      manifest(std::string(TerrainMaterialId), "DataAsset", "clay.json",
               R"({"content_type":"terrain_material","tags":[]})"),
      // Declares the right content type but names a map slot that does not
      // exist, so only the material loader can reject it.
      manifest(std::string(BrokenMaterialId), "DataAsset", "broken.json",
               R"({"content_type":"terrain_material","tags":[]})"),
      manifest(std::string(AudioId), "AudioClip", "wind.audio.json"),
      manifest(std::string(SetId), "DataAsset", "set.json",
               R"({"content_type":"terrain_material_set","schema":")" +
                   std::string(SchemaId) + R"(","tags":[]})",
               {std::string(SchemaId), std::string(RenderMaterialId),
                std::string(TerrainMaterialId)}),
  };
  std::ranges::sort(built.assets, {}, &AssetManifest::id);
  return built;
}

void writeSet(const std::string_view document) {
  assert(write(root / "set.json", document));
}

// Returns the rejection message, or an empty string when the document loaded.
std::string rejection(const std::string_view document) {
  writeSet(document);
  try {
    (void)loadTerrainMaterialSet(registry, SetId);
  } catch (const std::invalid_argument &failure) {
    return failure.what();
  }
  return {};
}

void expectRejected(const std::string_view document,
                    const std::string_view expected) {
  const std::string message = rejection(document);
  assert(!message.empty());
  assert(message.find(expected) != std::string::npos);
}

std::optional<TerrainMaterialSet> load(const std::string_view document) {
  writeSet(document);
  return loadTerrainMaterialSet(registry, SetId);
}

std::string describe(const TerrainMaterialSet &set) {
  std::string text = set.id + "|" + set.name + "|" + set.description;
  for (const auto &[role, material] : set.roles)
    text += "|" + role + "=" + material;
  for (const std::string &required : set.requiredRoles)
    text += "|" + required;
  return text;
}

constexpr std::string_view Minimal =
    R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://materials/stone"}})json";

constexpr std::string_view Full = R"json({
  "format_version": 1,
  "name": "Terrain PBR surfaces",
  "description": "Surface roles for generated terrain.",
  "roles": {
    "rock": "asset://materials/stone",
    "cliff": "asset://materials/stone",
    "sediment": "asset://materials/clay",
    "ground": "asset://materials/clay",
    "grass": "asset://materials/clay",
    "sand": "asset://materials/clay",
    "snow": "asset://materials/clay",
    "wet_ground": "asset://materials/clay",
    "underwater": "asset://materials/clay"
  },
  "required_roles": ["cliff", "grass", "rock", "snow", "wet_ground", "rock"]
})json";

bool containsCode(const Diagnostics &diagnostics, const std::string_view code) {
  return std::ranges::any_of(diagnostics, [&](const Diagnostic &item) {
    return item.code == code;
  });
}

void validSetLoads() {
  const auto minimal = load(Minimal);
  assert(minimal.has_value());
  assert(minimal->id == SetId);
  assert(minimal->name == "Test");
  assert(minimal->description.empty());
  assert(minimal->requiredRoles.empty());
  assert(minimal->roles.size() == 1);
  assert(minimal->roles.at("rock") == RenderMaterialId);

  const auto full = load(Full);
  assert(full.has_value());
  assert(full->name == "Terrain PBR surfaces");
  assert(full->description == "Surface roles for generated terrain.");
  assert(full->roles.size() == 9);
  // A render Material and a terrain_material DataAsset are both material
  // carriers, so a set may bind either.
  assert(full->roles.at("rock") == RenderMaterialId);
  assert(full->roles.at("sediment") == TerrainMaterialId);
  // required_roles is normalized, so a duplicated authored entry collapses.
  assert((full->requiredRoles == std::vector<std::string>{"cliff", "grass",
                                                           "rock", "snow",
                                                           "wet_ground"}));
}

void roleVocabularyRoundTrips() {
  constexpr TerrainMaterialRole roles[]{
      TerrainMaterialRole::Rock,      TerrainMaterialRole::Cliff,
      TerrainMaterialRole::Sediment,  TerrainMaterialRole::Ground,
      TerrainMaterialRole::Grass,     TerrainMaterialRole::Sand,
      TerrainMaterialRole::Snow,      TerrainMaterialRole::WetGround,
      TerrainMaterialRole::Underwater};
  std::set<std::string_view, std::less<>> names;
  for (const TerrainMaterialRole role : roles) {
    const std::string_view name = terrainMaterialRoleName(role);
    assert(!name.empty());
    assert(names.insert(name).second);
    const auto parsed = terrainMaterialRoleFromName(name);
    assert(parsed.has_value());
    assert(*parsed == role);
  }
  assert(names.size() == 9);
  assert(!terrainMaterialRoleFromName("moss").has_value());
  assert(!terrainMaterialRoleFromName("").has_value());
  // The documented spellings are stable identity, not presentation.
  assert(terrainMaterialRoleName(TerrainMaterialRole::Rock) == "rock");
  assert(terrainMaterialRoleName(TerrainMaterialRole::WetGround) ==
         "wet_ground");
  assert(terrainMaterialRoleName(TerrainMaterialRole::Underwater) ==
         "underwater");
}

void rejectsInvalidDocumentHeader() {
  // The DataAsset layer rejects a wrong format_version before the set rules run,
  // so the set's own header rules are exercised through the document entry
  // point a Data.load consumer uses.
  auto rejectionMessage = [](const std::string_view document) -> std::string {
    const DataDocumentResult parsed = parseDataDocument(document);
    assert(parsed.document != nullptr);
    try {
      (void)parseTerrainMaterialSet(*parsed.document, registry, SetId);
    } catch (const std::invalid_argument &failure) {
      return std::string(failure.what());
    }
    return std::string();
  };
  assert(rejectionMessage(
             R"json({"format_version":2,"name":"Test","roles":{"rock":"asset://materials/stone"}})json")
             .find("format_version: must be 1") != std::string::npos);
  assert(rejectionMessage(R"json({"name":"Test","roles":{"rock":"asset://materials/stone"}})json")
             .find("format_version: is required") != std::string::npos);
  assert(rejectionMessage(R"([1,2,3])")
             .find("document: must be an object") != std::string::npos);
  expectRejected(R"json({"format_version":1,"roles":{"rock":"asset://materials/stone"}})json",
                 "name: is required");
  expectRejected(R"json({"format_version":1,"name":"","roles":{"rock":"asset://materials/stone"}})json",
                 "name: must be a non-empty string");
  expectRejected(R"json({"format_version":1,"name":3,"roles":{"rock":"asset://materials/stone"}})json",
                 "name: must be a string");
  expectRejected(R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://materials/stone"},"description":7})json",
                 "description: must be a string");
  // A misspelled top-level key would otherwise be silently ignored.
  expectRejected(R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://materials/stone"},"require_roles":["rock"]})json",
                 "document: has unknown field \"require_roles\"");
}

void rejectsUnknownRole() {
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"moss":"asset://materials/stone"}})json",
      "roles/moss: is not a known role");
  // The message has to name the vocabulary, otherwise a typo stays a typo.
  assert(rejection(
             R"json({"format_version":1,"name":"Test","roles":{"moss":"asset://materials/stone"}})json")
             .find("underwater") != std::string::npos);
  // A scatter prop is not a surface role; the vocabulary deliberately does not
  // cover props.
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"tree":"asset://materials/stone"}})json",
      "roles/tree: is not a known role");
}

void rejectsEmptyRoles() {
  expectRejected(R"json({"format_version":1,"name":"Test"})json",
                 "roles: is required");
  expectRejected(R"json({"format_version":1,"name":"Test","roles":{}})json",
                 "roles: must be a non-empty object keyed by role name");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":[{"asset":"asset://materials/stone"}]})json",
      "roles: must be a non-empty object keyed by role name");
}

void rejectsInvalidRoleMaterial() {
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"rock":3}})json",
      "roles/rock: must be a string");
  expectRejected(R"json({"format_version":1,"name":"Test","roles":{"rock":""}})json",
                 "roles/rock: must not be empty");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"rock":"materials/stone"}})json",
      "roles/rock: must be an asset:// reference");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://materials/absent"}})json",
      "roles/rock: does not resolve: asset://materials/absent");
  // A role must bind a material, not something else that resolves.
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://audio/wind"}})json",
      "roles/rock: is not a material asset: asset://audio/wind is AudioClip");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://data/notes"}})json",
      "roles/rock: is not a material asset: asset://data/notes is DataAsset");
  // A material that declares the right content type still has to load, and the
  // message has to name the role that pulled it in.
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://materials/broken"}})json",
      "roles/rock: references a material that does not load");
  assert(rejection(
             R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://materials/broken"}})json")
             .find("has unknown map slot \"nomal\"") != std::string::npos);
}

void rejectsInvalidRequiredRoles() {
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://materials/stone"},"required_roles":{}})json",
      "required_roles: must be an array of role names");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://materials/stone"},"required_roles":["moss"]})json",
      "required_roles[0]: is not a known role");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://materials/stone"},"required_roles":["snow"]})json",
      "requires role \"snow\", which the set does not define");
  expectRejected(
      R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://materials/stone"},"required_roles":[3]})json",
      "required_roles[0]: must be a string");
  const auto loaded = load(
      R"json({"format_version":1,"name":"Test","roles":{"rock":"asset://materials/stone","sediment":"asset://materials/clay"},"required_roles":["sediment","rock"]})json");
  assert(loaded.has_value());
  assert((loaded->requiredRoles ==
          std::vector<std::string>{"rock", "sediment"}));
}

void dependenciesAreSortedAndDeduped() {
  const auto loaded = load(Full);
  assert(loaded.has_value());
  // Nine roles share two materials, so the dependency list names each once and
  // in sorted order.
  const std::vector<std::string> expected{"asset://materials/clay",
                                          "asset://materials/stone"};
  assert(loaded->assetDependencies() == expected);
  // Repeating the query cannot reorder or duplicate anything.
  assert(loaded->assetDependencies() == loaded->assetDependencies());
  assert(loaded->assetDependencies().size() == 2);
  assert(std::ranges::none_of(loaded->assetDependencies(),
                              [](const std::string &id) {
                                return id == "asset://schemas/terrain_material_set";
                              }));
}

void loadingIsDeterministic() {
  const std::string first = describe(*load(Full));
  const std::string second = describe(*load(Full));
  assert(first == second);
  // A role map is key-sorted, so a differently ordered document is identical.
  assert(describe(*load(
             R"json({"roles":{"sediment":"asset://materials/clay"},"name":"Order","format_version":1})json")) ==
         describe(*load(
             R"json({"format_version":1,"name":"Order","roles":{"sediment":"asset://materials/clay"}})json")));
}

void rejectsWrongManifestKind() {
  writeSet(Minimal);
  try {
    (void)loadTerrainMaterialSet(registry, RenderMaterialId);
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(std::string(failure.what())
               .find("must be a DataAsset: asset://materials/stone is Material") !=
           std::string::npos);
  }
  try {
    (void)loadTerrainMaterialSet(registry, SchemaId);
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(std::string(failure.what())
               .find("must be a DataAsset: asset://schemas/terrain_material_set "
                     "is DataSchema") != std::string::npos);
  }
  // A DataAsset with a different content type is a different document kind,
  // even though it carries the same role references.
  try {
    (void)loadTerrainMaterialSet(registry, TerrainMaterialId);
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(std::string(failure.what())
               .find("must declare settings.content_type "
                     "\"terrain_material_set\"") != std::string::npos);
  }
  try {
    (void)loadTerrainMaterialSet(registry, "asset://materials/absent");
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(std::string(failure.what())
               .find("was not found: asset://materials/absent") !=
           std::string::npos);
  }
}

void rejectsUndeclaredDependency() {
  writeSet(Full);
  assert(!hasErrors(validateDataAssets(registry)));
  // The loader does not read manifest dependencies, so declaring them is the
  // DataAsset layer's contract and it has to fail a set that omits one.
  AssetRegistry undeclared = registry;
  for (AssetManifest &asset : undeclared.assets)
    if (asset.id == SetId)
      asset.dependencies = {std::string(SchemaId)};
  const Diagnostics diagnostics = validateDataAssets(undeclared);
  assert(containsCode(diagnostics, "DATA_DEPENDENCY_UNDECLARED"));
  assert(std::ranges::any_of(diagnostics, [&](const Diagnostic &item) {
    return item.message.find("asset://materials/stone") != std::string::npos;
  }));
}

void exampleMaterialSetLoadsFromDisk() {
  const AssetRegistry example =
      loadAuthoredAssetRegistry(DEMI_SOURCE_DIR "/examples/terrain_3d");
  const std::optional<TerrainMaterialSet> set =
      loadTerrainMaterialSet(example, "asset://terrain/materials/terrain_pbr");
  assert(set.has_value());
  assert(set->name == "Terrain PBR surfaces");
  assert(!set->description.empty());
  assert(set->roles.size() == 8);
  assert(set->roles.at("rock") == "asset://terrain/surfaces/exposed_rock");
  assert(set->roles.at("cliff") == "asset://terrain/surfaces/cliff_piece");
  assert(set->roles.at("snow") == "asset://terrain/surfaces/snow");
  assert(set->roles.at("wet_ground") == "asset://terrain/surfaces/wet_ground");
  // Every role target must resolve as a material in the same project.
  for (const auto &[role, material] : set->roles) {
    (void)role;
    const AssetManifest *target = findAsset(example, material);
    assert(target != nullptr);
    assert(target->type == "Material");
  }
  assert((set->requiredRoles == std::vector<std::string>{"cliff", "grass",
                                                          "rock", "snow",
                                                          "wet_ground"}));
  const std::vector<std::string> dependencies = set->assetDependencies();
  // ground and sediment share the soil material.
  assert(dependencies.size() == 7);
  assert(std::is_sorted(dependencies.begin(), dependencies.end()));
  // Cook follows the manifest, so every reference the document makes has to be
  // declared there or the set ships without its materials.
  const AssetManifest *declared = findAsset(example, set->id);
  assert(declared != nullptr);
  for (const std::string &dependency : dependencies)
    assert(std::ranges::find(declared->dependencies, dependency) !=
           declared->dependencies.end());
  // The checked-in example must also satisfy the DataAsset schema contract.
  assert(!hasErrors(validateDataAssets(example)));
}

} // namespace

int main() {
  root = std::filesystem::temp_directory_path() / "demi_material_set_tests";
  std::error_code filesystemError;
  // Removed up front as well as on success, so a previous failed run cannot
  // leave a tree behind that changes what a rejection proves.
  std::filesystem::remove_all(root, filesystemError);
  std::filesystem::create_directories(root, filesystemError);
  // Only the DataAsset sources are ever read by these tests; the role targets
  // are resolved by id.
  assert(write(root / "notes.json", R"({"format_version":1,"note":"x"})"));
  assert(write(root / "clay.json",
               R"({"format_version":1,"name":"Clay"})"));
  assert(write(root / "broken.json",
               R"({"format_version":1,"name":"Broken","maps":[{"nomal":"asset://materials/clay"}]})"));
  assert(write(root / "schema.json", R"({
        "format_version": 1,
        "type": "object",
        "required": ["format_version", "name", "roles"],
        "properties": {
          "format_version": {"type": "integer", "enum": [1]},
          "name": {"type": "string"},
          "description": {"type": "string"},
          "roles": {
            "type": "object",
            "properties": {
              "rock": {"type": "string", "reference": "asset"},
              "cliff": {"type": "string", "reference": "asset"},
              "sediment": {"type": "string", "reference": "asset"},
              "ground": {"type": "string", "reference": "asset"},
              "grass": {"type": "string", "reference": "asset"},
              "sand": {"type": "string", "reference": "asset"},
              "snow": {"type": "string", "reference": "asset"},
              "wet_ground": {"type": "string", "reference": "asset"},
              "underwater": {"type": "string", "reference": "asset"}
            }
          },
          "required_roles": {"type": "array", "items": {"type": "string"}}
        }
      })"));
  registry = buildRegistry();
  writeSet(Minimal);

  validSetLoads();
  roleVocabularyRoundTrips();
  rejectsInvalidDocumentHeader();
  rejectsUnknownRole();
  rejectsEmptyRoles();
  rejectsInvalidRoleMaterial();
  rejectsInvalidRequiredRoles();
  dependenciesAreSortedAndDeduped();
  loadingIsDeterministic();
  rejectsWrongManifestKind();
  rejectsUndeclaredDependency();
  exampleMaterialSetLoadsFromDisk();

  std::filesystem::remove_all(root, filesystemError);
  std::cout << "Terrain material set checks passed\n";
  return 0;
}