#include "demi/assets/AssetImporter.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/DataDocument.h"
#include "demi/runtime/data/DataAssetStore.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool containsCode(const demi::Diagnostics &diagnostics,
                  const std::string_view code) {
  return std::ranges::any_of(diagnostics, [&](const demi::Diagnostic &item) {
    return item.code == code;
  });
}

bool containsCodeAt(const demi::Diagnostics &diagnostics,
                    const std::string_view code,
                    const std::string_view pointer) {
  return std::ranges::any_of(diagnostics, [&](const demi::Diagnostic &item) {
    return item.code == code && item.path.ends_with(pointer);
  });
}

bool write(const std::filesystem::path &path, const std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  return output.good();
}

demi::AssetManifest dataManifest(const std::filesystem::path &root,
                                 std::string id, std::string source,
                                 std::string contentType,
                                 std::string sourceHash = "hash:1") {
  return {.id = std::move(id),
          .type = "DataAsset",
          .importer = "json_data",
          .importerVersion = 1,
          .sourceHash = std::move(sourceHash),
          .settingsJson = "{\"content_type\":\"" + contentType +
                          "\",\"tags\":[\"shop:forest\",\"rarity:common\"]}",
          .manifestPath = root / (source + ".asset.json"),
          .sourcePath = root / source,
          .sourcePaths = {root / source}};
}

} // namespace

int main() {
  using namespace demi;
  using namespace demi::assets;
  using namespace demi::runtime;

  const auto dataImporter = importerFor("content.json", "DataAsset");
  const auto materialImporter = importerFor("material.json", "Material");
  if (!dataImporter || dataImporter->name != "json_data" ||
      dataImporter->assetType != "DataAsset" || !materialImporter ||
      materialImporter->name != "material" ||
      importerFor("unknown.json", "ArbitraryType")) {
    std::cerr << "Typed JSON importer selection failed.\n";
    return 1;
  }

  const auto allTypes = parseDataDocument(R"json({
    "format_version": 1,
    "null": null,
    "boolean": true,
    "integer": -7,
    "number": 1.25,
    "string": "mira",
    "numeric_key": {"1": "object"},
    "empty_object": {},
    "empty_array": [],
    "array": [1, false, null]
  })json");
  if (!allTypes.document || hasErrors(allTypes.diagnostics) ||
      !allTypes.document->root().find("null")->isNull() ||
      !allTypes.document->root().find("integer")->isInteger() ||
      !allTypes.document->root().find("number")->isNumber() ||
      !allTypes.document->root().find("numeric_key")->isObject() ||
      !allTypes.document->root().find("empty_object")->isObject() ||
      !allTypes.document->root().find("empty_array")->isArray() ||
      allTypes.document->root().find("array")->array()->size() != 3) {
    std::cerr << "DataDocument did not preserve JSON value kinds.\n";
    return 1;
  }

  if (!containsCode(
          parseDataDocument("[[]]", {}, {.maximumDepth = 0}).diagnostics,
          "DATA_DOCUMENT_DEPTH_EXCEEDED") ||
      !containsCode(
          parseDataDocument("[1,2]", {}, {.maximumElements = 2}).diagnostics,
          "DATA_DOCUMENT_ELEMENTS_EXCEEDED") ||
      !containsCode(parseDataDocument("\"long\"", {}, {.maximumStringBytes = 3})
                        .diagnostics,
                    "DATA_STRING_TOO_LARGE") ||
      !containsCode(parseDataDocument("18446744073709551615").diagnostics,
                    "DATA_NUMBER_OUT_OF_RANGE") ||
      !containsCode(parseDataDocument(std::string("\xC0\xAF", 2)).diagnostics,
                    "DATA_DOCUMENT_INVALID_UTF8") ||
      !containsCode(
          parseDataDocument("[]", {}, {.maximumBytes = 1}).diagnostics,
          "DATA_DOCUMENT_TOO_LARGE") ||
      !containsCode(parseDataDocument("{").diagnostics,
                    "DATA_DOCUMENT_INVALID_JSON")) {
    std::cerr << "DataDocument input limits missed a failure edge case.\n";
    return 1;
  }

  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "demi_data_asset_tests";
  std::error_code filesystemError;
  std::filesystem::remove_all(root, filesystemError);
  std::filesystem::create_directories(root, filesystemError);
  if (!write(root / "import_source.json",
             R"({"format_version":1,"value":"imported"})"))
    return 1;
  const auto imported = importAsset({.projectDirectory = root / "imported",
                                     .source = root / "import_source.json",
                                     .id = "asset://data/imported"});
  Diagnostic importDiagnostic;
  const auto importedManifest =
      loadAssetManifest(imported.manifestPath, &importDiagnostic);
  const auto importedMetadata =
      importedManifest ? dataAssetMetadata(*importedManifest) : std::nullopt;
  if (hasErrors(imported.diagnostics) || !importedManifest ||
      importedManifest->type != "DataAsset" || !importedMetadata ||
      importedMetadata->contentType != "data") {
    std::cerr << "Default JSON import did not create a usable DataAsset.\n";
    return 1;
  }
  if (!write(root / "sword.json",
             R"({"format_version":1,"id":"iron_sword","damage":7})") ||
      !write(root / "potion.json",
             R"({"format_version":1,"id":"potion","damage":0})")) {
    std::cerr << "Could not create DataAsset fixtures.\n";
    return 1;
  }
  AssetRegistry registry{.projectDirectory = root};
  registry.assets = {
      dataManifest(root, "asset://items/iron_sword", "sword.json", "item"),
      dataManifest(root, "asset://items/potion", "potion.json", "item")};
  std::ranges::sort(registry.assets, {}, &AssetManifest::id);

  DataAssetStore store;
  if (hasErrors(store.replace(registry)) ||
      store.revision("asset://items/iron_sword") != 1 ||
      store.query({.contentType = "item", .tags = {"shop:forest"}}).size() !=
          2) {
    std::cerr << "DataAssetStore did not load or query deterministic data.\n";
    return 1;
  }

  AssetRegistry incompatible = registry;
  incompatible.assets.front().settingsJson =
      R"({"content_type":"weapon","tags":["shop:forest"]})";
  incompatible.assets.front().sourceHash = "hash:3";
  if (!containsCode(store.replace(incompatible), "DATA_CONTENT_TYPE_CHANGED") ||
      store.revision("asset://items/iron_sword") != 1) {
    std::cerr << "Incompatible reload did not preserve the prior snapshot.\n";
    return 1;
  }
  const auto firstSnapshot = store.load("asset://items/iron_sword");
  std::string acquireError;
  const std::vector<std::string> shared{"asset://items/iron_sword"};
  if (!store.acquire("scene:a", shared, acquireError) ||
      !store.acquire("scene:b", shared, acquireError) ||
      store.referenceCount("asset://items/iron_sword") != 2) {
    std::cerr << "Shared DataAsset ownership was not reference counted.\n";
    return 1;
  }
  store.release("scene:a");
  if (store.referenceCount("asset://items/iron_sword") != 1) {
    std::cerr << "Releasing one owner dropped another owner's reference.\n";
    return 1;
  }
  const std::vector<std::string> missing{"asset://missing"};
  if (store.acquire("scene:failed", missing, acquireError) ||
      store.referenceCount("asset://items/iron_sword") != 1) {
    std::cerr << "Failed ownership acquisition was not atomic.\n";
    return 1;
  }

  AssetRegistry missingReferenced = registry;
  missingReferenced.assets.erase(missingReferenced.assets.begin());
  if (!containsCode(store.replace(missingReferenced),
                    "DATA_ASSET_STILL_REFERENCED") ||
      store.load("asset://items/iron_sword") != firstSnapshot) {
    std::cerr << "Failed DataAsset replacement did not retain old snapshots.\n";
    return 1;
  }

  if (!write(root / "sword.json",
             R"({"format_version":1,"id":"iron_sword","damage":9})"))
    return 1;
  registry.assets.back().dependencies = {"asset://items/iron_sword"};
  registry.assets.front().sourceHash = "hash:2";
  if (hasErrors(store.replace(registry)) ||
      store.revision("asset://items/iron_sword") != 2 ||
      store.reloadEvents().size() != 1 ||
      store.reloadEvents().front().oldRevision != 1 ||
      store.reloadEvents().front().affectedDependents !=
          std::vector<std::string>{"asset://items/potion"} ||
      firstSnapshot->document->root().find("damage") == nullptr ||
      std::get<std::int64_t>(
          firstSnapshot->document->root().find("damage")->value) != 7) {
    std::cerr << "Revisioned reload mutated or lost the previous snapshot.\n";
    return 1;
  }

  if (!write(root / "schema.json", R"({
      "format_version": 1,
      "type": "object",
      "required": ["format_version", "friend"],
      "properties": {
        "format_version": {"type": "integer", "enum": [1]},
        "friend": {"type": "string", "reference": "asset"},
        "level": {"type": "integer", "minimum": 1, "maximum": 10}
      }
    })") ||
      !write(
          root / "character.json",
          R"({"format_version":1,"friend":"asset://items/potion","level":4})"))
    return 1;
  AssetManifest schema{.id = "asset://schemas/character",
                       .type = "DataSchema",
                       .importer = "json_data",
                       .importerVersion = 1,
                       .sourceHash = "schema:1",
                       .manifestPath = root / "schema.asset.json",
                       .sourcePath = root / "schema.json",
                       .sourcePaths = {root / "schema.json"}};
  AssetManifest character = dataManifest(root, "asset://characters/mira",
                                         "character.json", "character");
  character.settingsJson =
      R"({"schema":"asset://schemas/character","content_type":"character","tags":[]})";
  character.dependencies = {"asset://schemas/character",
                            "asset://items/potion"};
  AssetRegistry schemaRegistry = registry;
  schemaRegistry.assets.push_back(schema);
  schemaRegistry.assets.push_back(character);
  std::ranges::sort(schemaRegistry.assets, {}, &AssetManifest::id);
  if (hasErrors(validateDataAssets(schemaRegistry))) {
    std::cerr << "A valid schema-backed DataAsset was rejected.\n";
    return 1;
  }
  write(root / "character.json",
        R"({"format_version":1,"friend":"missing","level":11})");
  const Diagnostics schemaErrors = validateDataAssets(schemaRegistry);
  if (!containsCodeAt(schemaErrors, "DATA_REFERENCE_INVALID", "#/friend") ||
      !containsCodeAt(schemaErrors, "DATA_SCHEMA_MAXIMUM", "#/level")) {
    std::cerr << "Schema validation missed reference and range errors.\n";
    return 1;
  }

  write(root / "character.json",
        R"({"format_version":1,"friend":"asset://items/missing","level":4})");
  if (!containsCodeAt(validateDataAssets(schemaRegistry),
                      "DATA_REFERENCE_NOT_FOUND", "#/friend")) {
    std::cerr << "Schema validation missed a missing stable asset reference.\n";
    return 1;
  }

  write(root / "schema.json", R"({"format_version":2,"type":"object"})");
  if (!containsCodeAt(validateDataAssets(schemaRegistry),
                      "DATA_SCHEMA_VERSION_UNSUPPORTED", "#/format_version")) {
    std::cerr << "A future schema version was accepted.\n";
    return 1;
  }

  std::filesystem::remove_all(root, filesystemError);
  return 0;
}
