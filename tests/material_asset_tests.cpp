#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/DataDocument.h"
#include "demi/assets/MaterialAsset.h"

#include <nlohmann/json.hpp>

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

constexpr std::string_view MaterialId = "asset://materials/stone";
constexpr std::string_view SchemaId = "asset://schemas/terrain_material";
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
          .importer = type == "DataSchema" ? "json_schema" : "json_data",
          .importerVersion = 1,
          .sourceHash = "hash:1",
          .dependencies = std::move(dependencies),
          .settingsJson = std::move(settings),
          .manifestPath = root / (source + ".asset.json"),
          .sourcePath = root / source,
          .sourcePaths = {root / source}};
}

// The slots the vocabulary round trip needs in the registry.
constexpr std::string_view TextureSlots[]{
    "base_color", "normal", "roughness", "metallic", "ambient_occlusion",
    "height", "detail", "emissive"};

// One registry serves every case: the material document is rewritten between
// loads, so a rejection proves the loader read the document under test.
AssetRegistry buildRegistry() {
  AssetRegistry built;
  built.projectDirectory = root;
  std::vector<std::string> materialDependencies{std::string(SchemaId)};
  for (const std::string_view slot : TextureSlots)
    materialDependencies.push_back("asset://textures/" + std::string(slot));
  built.assets = {
      manifest(std::string(SchemaId), "DataSchema", "schema.json"),
      manifest(std::string(NotesId), "DataAsset", "notes.json",
               R"({"content_type":"item","tags":[]})"),
      manifest(std::string(MaterialId), "DataAsset", "stone.json",
               R"({"content_type":"terrain_material","schema":")" +
                   std::string(SchemaId) + R"(","tags":[]})",
               materialDependencies),
  };
  // The loader resolves a map by id and never decodes it, so the texture
  // sources are placeholders.
  for (const std::string_view slot : TextureSlots)
    built.assets.push_back(manifest("asset://textures/" + std::string(slot),
                                    "Texture",
                                    "textures/" + std::string(slot) + ".png"));
  std::ranges::sort(built.assets, {}, &AssetManifest::id);
  return built;
}

void writeMaterial(const std::string_view document) {
  assert(write(root / "stone.json", document));
}

// Returns the rejection message, or an empty string when the document loaded.
std::string rejection(const std::string_view document) {
  writeMaterial(document);
  try {
    (void)loadTerrainMaterialAsset(registry, MaterialId);
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

std::optional<TerrainMaterialAsset> load(const std::string_view document) {
  writeMaterial(document);
  return loadTerrainMaterialAsset(registry, MaterialId);
}

bool sameColor(const runtime::Color &left, const runtime::Color &right) {
  return left.r == right.r && left.g == right.g && left.b == right.b &&
         left.a == right.a;
}

bool sameMaterial(const TerrainMaterialAsset &left,
                  const TerrainMaterialAsset &right) {
  return left.id == right.id && left.name == right.name &&
         sameColor(left.baseColor, right.baseColor) &&
         left.roughness == right.roughness && left.metallic == right.metallic &&
         left.normalStrength == right.normalStrength &&
         left.tiling == right.tiling &&
         left.detailStrength == right.detailStrength &&
         left.triplanar == right.triplanar && left.maps == right.maps;
}

std::string describe(const TerrainMaterialAsset &material) {
  std::string text = material.id + "|" + material.name + "|" +
                     std::to_string(material.baseColor.r) + "|" +
                     std::to_string(material.baseColor.g) + "|" +
                     std::to_string(material.baseColor.b) + "|" +
                     std::to_string(material.baseColor.a) + "|" +
                     std::to_string(material.roughness) + "|" +
                     std::to_string(material.metallic) + "|" +
                     std::to_string(material.normalStrength) + "|" +
                     std::to_string(material.tiling) + "|" +
                     std::to_string(material.detailStrength) + "|" +
                     std::to_string(material.triplanar);
  for (const auto &[slot, reference] : material.maps)
    text += "|" + std::string(terrainMaterialMapSlotName(slot)) + "=" +
            reference;
  return text;
}

constexpr std::string_view Minimal =
    R"json({"format_version":1,"name":"Stone"})json";

constexpr std::string_view Full = R"json({
  "format_version": 1,
  "name": "Weathered granite",
  "base_color": [0.42, 0.41, 0.39, 1.0],
  "roughness": 0.65,
  "metallic": 0.05,
  "normal_strength": 2.5,
  "tiling": 4,
  "detail_strength": 0.35,
  "triplanar": true,
  "maps": [
    {"base_color": "asset://textures/base_color"},
    {"normal": "asset://textures/normal"},
    {"roughness": "asset://textures/roughness"},
    {"metallic": "asset://textures/metallic"},
    {"ambient_occlusion": "asset://textures/ambient_occlusion"},
    {"height": "asset://textures/height"},
    {"detail": "asset://textures/detail"},
    {"emissive": "asset://textures/emissive"}
  ]
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

void validMaterialLoads() {
  const auto minimal = load(Minimal);
  assert(minimal.has_value());
  assert(minimal->id == MaterialId);
  assert(minimal->name == "Stone");
  // Omitted fields keep their documented defaults instead of being
  // materialized, so a minimal material stays minimal.
  assert(sameColor(minimal->baseColor,
                   runtime::Color{.8F, .8F, .8F, 1.F}));
  assert(minimal->roughness == 0.8F);
  assert(minimal->metallic == 0.F);
  assert(minimal->normalStrength == 1.F);
  assert(minimal->tiling == 1.F);
  assert(minimal->detailStrength == 0.F);
  assert(!minimal->triplanar);
  assert(minimal->maps.empty());

  const auto full = load(Full);
  assert(full.has_value());
  assert(full->name == "Weathered granite");
  assert(full->baseColor.r == 0.42F && full->baseColor.g == 0.41F);
  assert(full->baseColor.b == 0.39F && full->baseColor.a == 1.F);
  assert(full->roughness == 0.65F);
  assert(full->metallic == 0.05F);
  assert(full->normalStrength == 2.5F);
  // An integer tiling is still a number: the document stores JSON integers and
  // doubles in the same variant.
  assert(full->tiling == 4.F);
  assert(full->detailStrength == 0.35F);
  assert(full->triplanar);
  assert(full->maps.size() == 8);
  assert(full->maps.at(TerrainMaterialMapSlot::BaseColor) ==
         "asset://textures/base_color");
  assert(full->maps.at(TerrainMaterialMapSlot::AmbientOcclusion) ==
         "asset://textures/ambient_occlusion");
  assert(full->maps.at(TerrainMaterialMapSlot::Emissive) ==
         "asset://textures/emissive");
}

void mapSlotVocabularyRoundTrips() {
  constexpr TerrainMaterialMapSlot slots[]{
      TerrainMaterialMapSlot::BaseColor,       TerrainMaterialMapSlot::Normal,
      TerrainMaterialMapSlot::Roughness,       TerrainMaterialMapSlot::Metallic,
      TerrainMaterialMapSlot::AmbientOcclusion,
      TerrainMaterialMapSlot::Height,          TerrainMaterialMapSlot::Detail,
      TerrainMaterialMapSlot::Emissive};
  std::set<std::string_view, std::less<>> names;
  for (const TerrainMaterialMapSlot slot : slots) {
    const std::string_view name = terrainMaterialMapSlotName(slot);
    assert(!name.empty());
    assert(names.insert(name).second);
    const auto parsed = terrainMaterialMapSlotFromName(name);
    assert(parsed.has_value());
    assert(*parsed == slot);
  }
  assert(names.size() == 8);
  assert(!terrainMaterialMapSlotFromName("nomal").has_value());
  assert(!terrainMaterialMapSlotFromName("").has_value());
  // The documented spellings are stable identity, not presentation.
  assert(terrainMaterialMapSlotName(TerrainMaterialMapSlot::BaseColor) ==
         "base_color");
  assert(terrainMaterialMapSlotName(TerrainMaterialMapSlot::Normal) ==
         "normal");
  assert(terrainMaterialMapSlotName(TerrainMaterialMapSlot::AmbientOcclusion) ==
         "ambient_occlusion");
  assert(terrainMaterialMapSlotName(TerrainMaterialMapSlot::Emissive) ==
         "emissive");

  // Every slot also survives a document round trip on its own.
  for (const TerrainMaterialMapSlot slot : slots) {
    const std::string name(terrainMaterialMapSlotName(slot));
    // Built as a whole document: appending to a complete JSON object would
    // produce invalid JSON. "maps" is an array of single-slot objects, which is
    // what makes a duplicated slot representable and therefore rejectable.
    const std::string document =
        std::string("{\"format_version\":1,\"name\":\"Stone\",\"maps\":[{\"") +
        name + "\":\"asset://textures/" + name + "\"}]}";
    const auto loaded = load(document);
    assert(loaded.has_value());
    assert(loaded->maps.size() == 1);
    assert(loaded->maps.contains(slot));
    assert(loaded->maps.at(slot) == "asset://textures/" + name);
  }
}

void rejectsInvalidDocumentHeader() {
  // The DataAsset layer rejects a wrong format_version before the material rules
  // run, so the material's own header rules are exercised through the document
  // entry point a Data.load consumer uses.
  auto rejectionMessage = [](const std::string_view document) -> std::string {
    const DataDocumentResult parsed = parseDataDocument(document);
    assert(parsed.document != nullptr);
    try {
      (void)parseTerrainMaterialAsset(*parsed.document, registry, MaterialId);
    } catch (const std::invalid_argument &failure) {
      return std::string(failure.what());
    }
    return std::string();
  };
  assert(rejectionMessage(R"json({"format_version":2,"name":"Stone"})json")
             .find("format_version: must be 1") != std::string::npos);
  assert(rejectionMessage(R"json({"name":"Stone"})json")
             .find("format_version: is required") != std::string::npos);
  assert(rejectionMessage(R"json({"format_version":"1","name":"Stone"})json")
             .find("format_version: must be 1") != std::string::npos);
  assert(rejectionMessage(R"([1,2,3])")
             .find("document: must be an object") != std::string::npos);
  expectRejected(R"json({"format_version":1})json", "name: is required");
  expectRejected(R"json({"format_version":1,"name":""})json",
                 "name: must be a non-empty string");
  expectRejected(R"json({"format_version":1,"name":3})json",
                 "name: must be a string");
  // A misspelled top-level key would otherwise be silently ignored.
  expectRejected(R"json({"format_version":1,"name":"Stone","roughnes":0.5})json",
                 "document: has unknown field \"roughnes\"");
  // A document that does pass the material rules parses without a manifest.
  const DataDocumentResult parsed = parseDataDocument(Minimal);
  const auto material =
      parseTerrainMaterialAsset(*parsed.document, registry, MaterialId);
  assert(material.has_value());
  assert(material->id == MaterialId);
}

void rejectsInvalidScalarRanges() {
  expectRejected(R"json({"format_version":1,"name":"Stone","roughness":-0.1})json",
                 "roughness: must be between 0 and 1");
  expectRejected(R"json({"format_version":1,"name":"Stone","roughness":1.5})json",
                 "roughness: must be between 0 and 1");
  expectRejected(R"json({"format_version":1,"name":"Stone","metallic":-1})json",
                 "metallic: must be between 0 and 1");
  expectRejected(R"json({"format_version":1,"name":"Stone","metallic":"shiny"})json",
                 "metallic: must be a finite number");
  expectRejected(R"json({"format_version":1,"name":"Stone","normal_strength":4.5})json",
                 "normal_strength: must be between 0 and 4");
  expectRejected(R"json({"format_version":1,"name":"Stone","normal_strength":-1})json",
                 "normal_strength: must be between 0 and 4");
  expectRejected(R"json({"format_version":1,"name":"Stone","tiling":0})json",
                 "tiling: must be greater than 0");
  expectRejected(R"json({"format_version":1,"name":"Stone","tiling":-2})json",
                 "tiling: must be greater than 0");
  expectRejected(R"json({"format_version":1,"name":"Stone","detail_strength":-0.5})json",
                 "detail_strength: must be at least 0");
  expectRejected(R"json({"format_version":1,"name":"Stone","triplanar":"yes"})json",
                 "triplanar: must be a boolean");
  // The bounds themselves are legal values, not just their rejection.
  const auto bounded = load(
      R"json({"format_version":1,"name":"Stone","roughness":0,"metallic":1,"normal_strength":0})json");
  assert(bounded.has_value());
  assert(bounded->roughness == 0.F && bounded->metallic == 1.F);
  assert(bounded->normalStrength == 0.F);
  const auto amplified = load(
      R"json({"format_version":1,"name":"Stone","normal_strength":4})json");
  assert(amplified.has_value());
  assert(amplified->normalStrength == 4.F);
}

void rejectsInvalidColor() {
  expectRejected(R"json({"format_version":1,"name":"Stone","base_color":[1,1,1]})json",
                 "base_color: must be [r, g, b, a] with four channels");
  expectRejected(R"json({"format_version":1,"name":"Stone","base_color":"#804020"})json",
                 "base_color: must be [r, g, b, a] with four channels");
  expectRejected(R"json({"format_version":1,"name":"Stone","base_color":[1,1,1,"a"]})json",
                 "base_color[3]: must be a finite number");
  expectRejected(R"json({"format_version":1,"name":"Stone","base_color":[1,1,1,2]})json",
                 "base_color[3]: must be between 0 and 1");
}

void rejectsInvalidMapShape() {
  expectRejected(R"json({"format_version":1,"name":"Stone","maps":{}})json",
                 "maps: must be an array of single-slot objects");
  expectRejected(R"json({"format_version":1,"name":"Stone","maps":["normal"]})json",
                 "maps[0]: must name exactly one map slot");
  expectRejected(R"json({"format_version":1,"name":"Stone","maps":[{}]})json",
                 "maps[0]: must name exactly one map slot");
  expectRejected(
      R"json({"format_version":1,"name":"Stone","maps":[{"normal":"asset://textures/normal","detail":"asset://textures/detail"}]})json",
      "maps[0]: must name exactly one map slot");
  // An unknown slot is an error, not a map that silently never reaches the
  // shader.
  expectRejected(
      R"json({"format_version":1,"name":"Stone","maps":[{"nomal":"asset://textures/normal"}]})json",
      "maps[0]: has unknown map slot \"nomal\"");
  assert(rejection(
             R"json({"format_version":1,"name":"Stone","maps":[{"nomal":"asset://textures/normal"}]})json")
             .find("ambient_occlusion") != std::string::npos);
}

void rejectsInvalidMapReference() {
  expectRejected(
      R"json({"format_version":1,"name":"Stone","maps":[{"normal":3}]})json",
      "maps[0]/normal: must be a string");
  expectRejected(
      R"json({"format_version":1,"name":"Stone","maps":[{"normal":""}]})json",
      "maps[0]/normal: must not be empty");
  expectRejected(
      R"json({"format_version":1,"name":"Stone","maps":[{"normal":"textures/normal.png"}]})json",
      "maps[0]/normal: must be an asset:// reference");
  // The message has to name the slot, otherwise the author cannot tell which
  // map is missing.
  expectRejected(
      R"json({"format_version":1,"name":"Stone","maps":[{"detail":"asset://textures/absent"}]})json",
      "maps[0]/detail: does not resolve: asset://textures/absent");
}

void rejectsRepeatedSlot() {
  expectRejected(
      R"json({"format_version":1,"name":"Stone","maps":[{"normal":"asset://textures/normal"},{"normal":"asset://textures/normal"}]})json",
      "maps[1]/normal: repeats the normal slot");
  // The same slot through a different key is still a repeat, not a second map.
  expectRejected(
      R"json({"format_version":1,"name":"Stone","maps":[{"height":"asset://textures/height"},{"height":"asset://textures/height"}]})json",
      "maps[1]/height: repeats the height slot");
}

void rejectsDetailStrengthWithoutDetailMap() {
  expectRejected(
      R"json({"format_version":1,"name":"Stone","detail_strength":0.5,"maps":[{"normal":"asset://textures/normal"}]})json",
      "but no detail map is assigned");
  // A zero strength needs no detail map, and a detail map may exist unused.
  const auto zeroed = load(
      R"json({"format_version":1,"name":"Stone","detail_strength":0})json");
  assert(zeroed.has_value());
  assert(zeroed->detailStrength == 0.F);
  const auto mapped = load(
      R"json({"format_version":1,"name":"Stone","detail_strength":0.5,"maps":[{"detail":"asset://textures/detail"}]})json");
  assert(mapped.has_value());
  assert(mapped->detailStrength == 0.5F);
}

void roundTripIsLossless() {
  const auto original = load(Full);
  assert(original.has_value());
  const DataDocumentResult reparsed =
      parseDataDocument(original->toJson().dump());
  assert(reparsed.document != nullptr);
  const auto roundTripped =
      parseTerrainMaterialAsset(*reparsed.document, registry, MaterialId);
  assert(roundTripped.has_value());
  assert(sameMaterial(*original, *roundTripped));
  // Re-serializing the reparsed material is a fixed point, so a second save
  // produces no diff.
  assert(roundTripped->toJson().dump() == original->toJson().dump());
  // Defaults stay omitted rather than materialized into authored source.
  const auto minimal = load(Minimal);
  assert(minimal.has_value());
  const nlohmann::json minimalJson = minimal->toJson();
  assert(minimalJson.size() == 2);
  assert(minimalJson["format_version"] == 1);
  assert(minimalJson["name"] == "Stone");
  assert(!minimalJson.contains("roughness"));
  assert(!minimalJson.contains("triplanar"));
  assert(!minimalJson.contains("maps"));
}

void dependenciesAreSortedAndDeduped() {
  const auto loaded = load(R"json({
    "format_version": 1,
    "name": "Granite",
    "maps": [
      {"normal": "asset://textures/normal"},
      {"base_color": "asset://textures/base_color"},
      {"detail": "asset://textures/normal"},
      {"height": "asset://textures/height"}
    ]
  })json");
  assert(loaded.has_value());
  // A map list is an array, so the same texture may be assigned to two slots;
  // the dependency list still names it once.
  const std::vector<std::string> expected{
      "asset://textures/base_color", "asset://textures/height",
      "asset://textures/normal"};
  assert(loaded->assetDependencies() == expected);
  // Repeating the query cannot reorder or duplicate anything.
  assert(loaded->assetDependencies() == loaded->assetDependencies());
  assert(loaded->assetDependencies().size() == 3);
  assert(load(Minimal)->assetDependencies().empty());
}

void loadingIsDeterministic() {
  const std::string first = describe(*load(Full));
  const std::string second = describe(*load(Full));
  assert(first == second);
  // Maps are stored slot-keyed, so a differently ordered document is identical.
  assert(describe(*load(R"json({"maps":[{"normal":"asset://textures/normal"},{"base_color":"asset://textures/base_color"}],"name":"Order","format_version":1})json")) ==
         describe(*load(R"json({"format_version":1,"name":"Order","maps":[{"base_color":"asset://textures/base_color"},{"normal":"asset://textures/normal"}]})json")));
}

void rejectsWrongManifestKind() {
  writeMaterial(Minimal);
  try {
    (void)loadTerrainMaterialAsset(registry, "asset://textures/base_color");
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(std::string(failure.what())
               .find("must be a DataAsset: asset://textures/base_color is Texture") !=
           std::string::npos);
  }
  try {
    (void)loadTerrainMaterialAsset(registry, "asset://schemas/terrain_material");
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(std::string(failure.what())
               .find("must be a DataAsset: asset://schemas/terrain_material is "
                     "DataSchema") != std::string::npos);
  }
  // A DataAsset with a different content type is a different document kind.
  try {
    (void)loadTerrainMaterialAsset(registry, NotesId);
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(std::string(failure.what())
               .find("must declare settings.content_type \"terrain_material\"") !=
           std::string::npos);
  }
  try {
    (void)loadTerrainMaterialAsset(registry, "asset://materials/absent");
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(std::string(failure.what())
               .find("was not found: asset://materials/absent") !=
           std::string::npos);
  }
}

void rejectsUndeclaredDependency() {
  writeMaterial(Full);
  assert(!hasErrors(validateDataAssets(registry)));
  // The loader does not read manifest dependencies, so declaring them is the
  // DataAsset layer's contract and it has to fail a material that omits one.
  AssetRegistry undeclared = registry;
  for (AssetManifest &asset : undeclared.assets)
    if (asset.id == MaterialId)
      asset.dependencies = {std::string(SchemaId)};
  const Diagnostics diagnostics = validateDataAssets(undeclared);
  assert(containsCode(diagnostics, "DATA_DEPENDENCY_UNDECLARED"));
  assert(containsMessage(diagnostics, "asset://textures/normal"));
  // Dropping the schema declaration fails the same way.
  AssetRegistry unschemaed = registry;
  for (AssetManifest &asset : unschemaed.assets)
    if (asset.id == MaterialId)
      asset.dependencies.clear();
  assert(containsCode(validateDataAssets(unschemaed),
                      "DATA_DEPENDENCY_UNDECLARED"));
}

void exampleProjectValidates() {
  // The checked-in example must satisfy the DataAsset schema contract.
  const AssetRegistry example =
      loadAuthoredAssetRegistry(DEMI_SOURCE_DIR "/examples/terrain_3d");
  assert(!hasErrors(validateDataAssets(example)));
  // Any terrain_material document the example ships has to load under the same
  // rules a hot reload or a Data.load consumer would apply.
  for (const AssetManifest &asset : example.assets) {
    const auto metadata =
        asset.type == "DataAsset" ? dataAssetMetadata(asset) : std::nullopt;
    if (!metadata || metadata->contentType != "terrain_material")
      continue;
    assert(loadTerrainMaterialAsset(example, asset.id).has_value());
  }
}

} // namespace

int main() {
  root = std::filesystem::temp_directory_path() / "demi_material_asset_tests";
  std::error_code filesystemError;
  // Removed up front as well as on success, so a previous failed run cannot
  // leave a tree behind that changes what a rejection proves.
  std::filesystem::remove_all(root, filesystemError);
  std::filesystem::create_directories(root, filesystemError);
  assert(write(root / "notes.json", R"({"format_version":1,"note":"x"})"));
  assert(write(root / "schema.json", R"({
        "format_version": 1,
        "type": "object",
        "required": ["format_version", "name"],
        "properties": {
          "format_version": {"type": "integer", "enum": [1]},
          "name": {"type": "string"},
          "base_color": {"type": "array", "items": {"type": "number"}},
          "roughness": {"type": "number", "minimum": 0, "maximum": 1},
          "metallic": {"type": "number", "minimum": 0, "maximum": 1},
          "normal_strength": {"type": "number", "minimum": 0, "maximum": 4},
          "tiling": {"type": "number"},
          "detail_strength": {"type": "number", "minimum": 0},
          "triplanar": {"type": "boolean"},
          "maps": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "base_color": {"type": "string", "reference": "asset"},
                "normal": {"type": "string", "reference": "asset"},
                "roughness": {"type": "string", "reference": "asset"},
                "metallic": {"type": "string", "reference": "asset"},
                "ambient_occlusion": {"type": "string", "reference": "asset"},
                "height": {"type": "string", "reference": "asset"},
                "detail": {"type": "string", "reference": "asset"},
                "emissive": {"type": "string", "reference": "asset"}
              }
            }
          }
        }
      })"));
  for (const std::string_view slot : TextureSlots)
    assert(write(root / "textures" / (std::string(slot) + ".png"),
                 "placeholder"));
  registry = buildRegistry();
  writeMaterial(Minimal);

  validMaterialLoads();
  mapSlotVocabularyRoundTrips();
  rejectsInvalidDocumentHeader();
  rejectsInvalidScalarRanges();
  rejectsInvalidColor();
  rejectsInvalidMapShape();
  rejectsInvalidMapReference();
  rejectsRepeatedSlot();
  rejectsDetailStrengthWithoutDetailMap();
  roundTripIsLossless();
  dependenciesAreSortedAndDeduped();
  loadingIsDeterministic();
  rejectsWrongManifestKind();
  rejectsUndeclaredDependency();
  exampleProjectValidates();

  std::filesystem::remove_all(root, filesystemError);
  std::cout << "Terrain material checks passed\n";
  return 0;
}