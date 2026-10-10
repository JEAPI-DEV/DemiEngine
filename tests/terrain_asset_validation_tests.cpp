#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/TerrainAssetValidation.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace demi;
using namespace demi::assets;

namespace {

constexpr std::string_view PaletteId = "asset://terrain/palettes/test";
constexpr std::string_view PaletteSchemaId = "asset://schemas/terrain_palette";
constexpr std::string_view SetId = "asset://terrain/material_sets/test";
constexpr std::string_view SetSchemaId =
    "asset://schemas/terrain_material_set";
constexpr std::string_view GrassId = "asset://terrain/surfaces/grass";
constexpr std::string_view RockId = "asset://terrain/surfaces/rock";
constexpr std::string_view ClayId = "asset://terrain/surfaces/clay";
constexpr std::string_view FlatId = "asset://terrain/surfaces/flat";
constexpr std::string_view ClayBaseId = "asset://terrain/textures/clay_base";
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

// The fixture hashes its sources exactly as an import would, so a manifest that
// claims a stale hash cannot pass unnoticed.
AssetManifest manifest(const std::string &id, const std::string &type,
                       const std::string &source, std::string settings = "{}",
                       std::vector<std::string> dependencies = {}) {
  const std::optional<std::string> hash = hashFiles({root / source});
  assert(hash.has_value());
  return {.id = id,
          .type = type,
          .importer = type == "DataSchema"
                          ? "json_schema"
                          : (type == "DataAsset" ? "json_data" : type),
          .importerVersion = 1,
          .sourceHash = *hash,
          .dependencies = std::move(dependencies),
          .settingsJson = std::move(settings),
          .manifestPath = root / (source + ".asset.json"),
          .sourcePath = root / source,
          .sourcePaths = {root / source}};
}

// Every art asset in this project is CC-BY-4.0 and may ship, so a clean run is
// clean without anybody having to declare the licence twice.
std::string shippableSettings(const std::string_view contentType) {
  return std::string("{\"content_type\":\"") + std::string(contentType) +
         "\",\"licence\":{\"spdx\":\"CC-BY-4.0\",\"redistributable\":true}}";
}

// One registry serves every case: the palette, set and material documents are
// rewritten between runs, so a finding proves the validator read the document
// under test.
AssetRegistry buildRegistry() {
  AssetRegistry built;
  built.projectDirectory = root;
  built.assets = {
      manifest(std::string(PaletteSchemaId), "DataSchema",
               "palette_schema.json"),
      manifest(std::string(SetSchemaId), "DataSchema", "set_schema.json"),
      manifest(std::string(GrassId), "Model3D", "oak.glb",
               shippableSettings("surface")),
      manifest(std::string(RockId), "Model3D", "oak.glb",
               shippableSettings("surface")),
      manifest("asset://terrain/surfaces/ground", "Material",
               "grass.material.json", shippableSettings("surface")),
      manifest(std::string(ClayId), "DataAsset", "clay.json",
               shippableSettings("terrain_material"),
               {std::string(ClayBaseId)}),
      manifest(std::string(FlatId), "DataAsset", "flat.json",
               shippableSettings("terrain_material")),
      manifest(std::string(ClayBaseId), "Texture2D", "clay_base.png",
               shippableSettings("albedo")),
      manifest(std::string(AudioId), "AudioClip", "wind.audio.json"),
      manifest(std::string(NotesId), "DataAsset", "notes.json",
               R"({"content_type":"item","tags":[]})"),
      manifest(std::string(PaletteId), "DataAsset", "palette.json",
               R"({"content_type":"terrain_palette","schema":")" +
                   std::string(PaletteSchemaId) + R"(","tags":[]})",
               {std::string(PaletteSchemaId), std::string(GrassId),
                std::string(RockId)}),
      manifest(std::string(SetId), "DataAsset", "set.json",
               R"({"content_type":"terrain_material_set","schema":")" +
                   std::string(SetSchemaId) + R"(","tags":[]})",
               {std::string(SetSchemaId), std::string(ClayId),
                "asset://terrain/surfaces/ground", std::string(FlatId)}),
  };
  std::ranges::sort(built.assets, {}, &AssetManifest::id);
  return built;
}

void writePalette(const std::string_view document) {
  assert(write(root / "palette.json", document));
}

void writeSet(const std::string_view document) {
  assert(write(root / "set.json", document));
}

void writeClay(const std::string_view document) {
  assert(write(root / "clay.json", document));
}

constexpr std::string_view PaletteDocument = R"json({
  "format_version": 2,
  "name": "Test meadow",
  "placements": {
    "grass": {
      "model": "asset://terrain/surfaces/grass",
      "weight": 1
    },
    "soil": {
      "model": "asset://terrain/surfaces/rock",
      "weight": 1,
      "scale": [
        1,
        2
      ]
    }
  }
})json";

constexpr std::string_view SetDocument = R"json({
  "format_version": 1,
  "name": "Test set",
  "roles": {"rock": "asset://terrain/surfaces/clay", "ground": "asset://terrain/surfaces/ground"},
  "required_roles": ["rock"]
})json";

// A colour-only terrain material: one albedo, no normal. Legitimate content, so
// the default request must accept it.
constexpr std::string_view ClayDocument = R"json({
  "format_version": 1,
  "name": "Clay",
  "maps": [{"base_color": "asset://terrain/textures/clay_base"}]
})json";

// A material with no maps at all: every surface value comes from scalars.
constexpr std::string_view FlatDocument =
    R"json({"format_version":1,"name":"Flat","roughness":0.9})json";

// Written before each case so a document rewritten by an earlier case cannot
// change what the next one proves.
void resetDocuments() {
  writePalette(PaletteDocument);
  writeSet(SetDocument);
  writeClay(ClayDocument);
  assert(write(root / "flat.json", FlatDocument));
}

TerrainAssetValidationRequest request(const AssetRegistry &source,
                                      const TerrainAssetUse use =
                                          TerrainAssetUse::Shippable,
                                      const bool requireFullMaterialMaps =
                                          false) {
  TerrainAssetValidationRequest built;
  built.registry = &source;
  built.use = use;
  built.paletteId = PaletteId;
  built.materialSetId = SetId;
  built.requireFullMaterialMaps = requireFullMaterialMaps;
  return built;
}

std::vector<TerrainAssetFinding>
ofKind(const TerrainAssetValidationReport &report,
       const TerrainAssetFindingKind kind) {
  std::vector<TerrainAssetFinding> selected;
  for (const TerrainAssetFinding &finding : report.findings)
    if (finding.kind == kind)
      selected.push_back(finding);
  return selected;
}

bool contains(const std::string &haystack, const std::string_view needle) {
  return haystack.find(needle) != std::string::npos;
}

bool hasFinding(const TerrainAssetValidationReport &report,
                const TerrainAssetFindingKind kind,
                const std::string_view needle) {
  return std::ranges::any_of(
      report.findings, [&](const TerrainAssetFinding &item) {
        return item.kind == kind && contains(item.message, needle);
      });
}

// Every message has to be actionable, so a message that cannot be read here is
// as much of a failure as a missing finding.
void expectFinding(const TerrainAssetValidationReport &report,
                   const TerrainAssetFindingKind kind,
                   const std::string_view needle) {
  if (!hasFinding(report, kind, needle))
    std::cerr << "expected a " << terrainAssetFindingKindName(kind)
              << " finding saying \"" << needle << "\", but the report said:\n"
              << report.summary() << "\n";
  assert(hasFinding(report, kind, needle));
}

// Rewrites one manifest without touching the rest, so a case changes exactly
// the thing it is about.
AssetRegistry withAsset(const AssetRegistry &source, const std::string &id,
                        const std::string &settings,
                        const std::vector<std::string> &dependencies) {
  AssetRegistry changed = source;
  bool found = false;
  for (AssetManifest &asset : changed.assets)
    if (asset.id == id) {
      asset.settingsJson = settings;
      asset.dependencies = dependencies;
      found = true;
    }
  assert(found);
  return changed;
}

std::string settingsOf(const AssetRegistry &source, const std::string &id) {
  for (const AssetManifest &asset : source.assets)
    if (asset.id == id)
      return asset.settingsJson;
  return {};
}

// A clean project answers nothing, which is the only useful way to prove every
// other case reports something.
void cleanProjectIsShippable() {
  resetDocuments();
  const TerrainAssetValidationReport report =
      validateTerrainAssets(request(registry));
  if (!report.findings.empty())
    std::cerr << report.summary() << "\n";
  assert(report.findings.empty());
  assert(report.blockingCount() == 0);
  assert(report.advisoryCount() == 0);
  assert(report.shippable());
  // The palette document, its two roles, the set document, its two roles and
  // the one map the clay material declares.
  assert(report.checked == 7);
  assert(report.use == TerrainAssetUse::Shippable);
  assert(contains(report.summary(), "no finding blocks this use"));
  assert(contains(report.summary(), "7 references checked"));
  // A mesh may replace a material for a palette role, so art is swappable
  // without touching the palette.
  AssetRegistry meshRegistry = registry;
  meshRegistry.assets.push_back(manifest("asset://terrain/props/oak", "Model3D",
                                         "oak.glb", shippableSettings("prop")));
  std::ranges::sort(meshRegistry.assets, {}, &AssetManifest::id);
  writePalette(R"json({
  "format_version": 2,
  "name": "Test meadow",
  "placements": {
    "tree": {
      "weight": 1,
      "model": "asset://terrain/props/oak"
    }
  }
})json");
  const TerrainAssetValidationReport mesh =
      validateTerrainAssets(request(meshRegistry));
  const auto noKind = [&](const TerrainAssetFindingKind kind) {
    return std::ranges::none_of(mesh.findings,
                                [&](const TerrainAssetFinding &item) {
                                  return item.kind == kind;
                                });
  };
  assert(noKind(TerrainAssetFindingKind::WrongAssetType));
  assert(noKind(TerrainAssetFindingKind::MissingRole));
}

// The palette loader rejects an unbound required role before anything is
// generated; this validator is what an author reads first, and it names the
// role that is unbound.
void unboundRequiredRoleIsReported() {
  resetDocuments();
  // A material set needs the same answer for a surface role.
  writeSet(R"json({
    "format_version": 1,
    "name": "Test set",
    "roles": {"rock": "asset://terrain/surfaces/clay"},
    "required_roles": ["rock", "underwater"]
  })json");
  const TerrainAssetValidationReport surfaces =
      validateTerrainAssets(request(registry));
  expectFinding(surfaces, TerrainAssetFindingKind::MissingRole, "underwater");
  expectFinding(surfaces, TerrainAssetFindingKind::MissingRole,
                "would leave that surface unshaded");
}

// A reference that does not resolve is the mistake that ships an empty biome.
void unresolvedReferenceBlocks() {
  resetDocuments();
  writePalette(R"json({
  "format_version": 2,
  "name": "Test meadow",
  "placements": {
    "grass": {
      "weight": 1,
      "model": "asset://terrain/surfaces/absent"
    }
  }
})json");
  const TerrainAssetValidationReport report =
      validateTerrainAssets(request(registry));
  assert(!report.shippable());
  assert(ofKind(report, TerrainAssetFindingKind::UnresolvedAsset).size() == 1);
  expectFinding(report, TerrainAssetFindingKind::UnresolvedAsset,
                "asset://terrain/surfaces/absent");
  expectFinding(report, TerrainAssetFindingKind::UnresolvedAsset,
                "Add the manifest for that id");

  // A bare path is the same mistake written differently, and it is named the
  // same way rather than silently finding nothing.
  writePalette(R"json({
  "format_version": 2,
  "name": "Test meadow",
  "placements": {
    "grass": {
      "weight": 1,
      "model": "terrain/surfaces/grass"
    }
  }
})json");
  const TerrainAssetValidationReport bare =
      validateTerrainAssets(request(registry));
  assert(!bare.shippable());
  expectFinding(bare, TerrainAssetFindingKind::UnresolvedAsset,
                "must be an asset:// reference");
  // An absent binding is not a reference at all, and says so.
  writePalette(R"json({
    "format_version": 2,
    "name": "Test meadow",
    "placements": {"grass": {"weight": 1}}
  })json");
  const TerrainAssetValidationReport unbound =
      validateTerrainAssets(request(registry));
  assert(!unbound.shippable());
  expectFinding(unbound, TerrainAssetFindingKind::UnresolvedAsset,
                "requires model or prefab");
  expectFinding(unbound, TerrainAssetFindingKind::UnresolvedAsset,
                "placements/grass/model");
  // The documents this validator is asked about can be missing too.
  TerrainAssetValidationRequest absentPalette;
  absentPalette.registry = &registry;
  absentPalette.paletteId = "asset://terrain/palettes/absent";
  const TerrainAssetValidationReport absent =
      validateTerrainAssets(absentPalette);
  assert(!absent.shippable());
  expectFinding(absent, TerrainAssetFindingKind::UnresolvedAsset,
                "asset://terrain/palettes/absent does not resolve");
  // A document whose shape hides every role is reported as uninspectable rather
  // than as a clean run, because "no findings" would be a lie there.
  writePalette(
      R"json({"format_version": 2, "name": "Test meadow", "placements": 7})json");
  const TerrainAssetValidationReport uninspectable =
      validateTerrainAssets(request(registry));
  assert(!uninspectable.shippable());
  expectFinding(uninspectable, TerrainAssetFindingKind::UnresolvedAsset,
                "placements must be an object");
  expectFinding(uninspectable, TerrainAssetFindingKind::UnresolvedAsset,
                "rule ID");
}

// An audio clip resolves perfectly well and is still not terrain art.
void wrongAssetTypeBlocks() {
  resetDocuments();
  writeSet(R"json({
    "format_version": 1,
    "name": "Test set",
    "roles": {"rock": "asset://data/notes"},
    "required_roles": ["rock"]
  })json");
  const TerrainAssetValidationReport report =
      validateTerrainAssets(request(registry));
  assert(!report.shippable());
  expectFinding(report, TerrainAssetFindingKind::WrongAssetType,
                "needs a material");
  // The message names what the asset actually is, not just that it is wrong.
  expectFinding(report, TerrainAssetFindingKind::WrongAssetType, "\"item\"");
  expectFinding(report, TerrainAssetFindingKind::WrongAssetType,
                "Point the role at a material asset");

  writePalette(R"json({
  "format_version": 2,
  "name": "Test meadow",
  "placements": {
    "grass": {
      "weight": 1,
      "model": "asset://audio/wind"
    }
  }
})json");
  const TerrainAssetValidationReport palette =
      validateTerrainAssets(request(registry));
  assert(!palette.shippable());
  expectFinding(palette, TerrainAssetFindingKind::WrongAssetType,
                "needs Model3D geometry");
  expectFinding(palette, TerrainAssetFindingKind::WrongAssetType, "AudioClip");

  writePalette(R"json({
  "format_version": 2,
  "name": "Test meadow",
  "placements": {
    "grass": {
      "weight": 1,
      "model": "asset://terrain/surfaces/ground"
    }
  }
})json");
  const auto materialOnly = validateTerrainAssets(request(registry));
  expectFinding(materialOnly, TerrainAssetFindingKind::WrongAssetType,
                "needs Model3D geometry");

  // A document of the wrong kind is caught too, not only the wrong manifest
  // type: the set cannot be read as a palette.
  TerrainAssetValidationRequest crossed;
  crossed.registry = &registry;
  crossed.paletteId = SetId;
  const TerrainAssetValidationReport crossedReport =
      validateTerrainAssets(crossed);
  assert(!crossedReport.shippable());
  expectFinding(crossedReport, TerrainAssetFindingKind::WrongAssetType,
                "settings.content_type \"terrain_palette\"");
}

// Cook follows the manifest, so a reference the document makes and the manifest
// omits is art that silently never ships. The data-asset validator owns that
// rule; this validator has to agree with it rather than invent its own.
void missingDependencyIsDetectedAndAgrees() {
  resetDocuments();
  const AssetRegistry undeclared =
      withAsset(registry, std::string(PaletteId),
                settingsOf(registry, std::string(PaletteId)),
                {std::string(PaletteSchemaId)});
  const TerrainAssetValidationReport report =
      validateTerrainAssets(request(undeclared));
  const std::vector<TerrainAssetFinding> missing =
      ofKind(report, TerrainAssetFindingKind::MissingDependency);
  assert(missing.size() == 2);
  expectFinding(report, TerrainAssetFindingKind::MissingDependency,
                "add it to the dependencies array");
  // The same registry, through the data-asset layer, names the same references.
  const Diagnostics diagnostics = validateDataAssets(undeclared);
  for (const TerrainAssetFinding &finding : missing) {
    assert(finding.assetId == GrassId || finding.assetId == RockId);
    bool named = false;
    for (const Diagnostic &item : diagnostics)
      if (item.code == "DATA_DEPENDENCY_UNDECLARED" &&
          contains(item.message, finding.assetId))
        named = true;
    if (!named)
      std::cerr << "data-asset validation did not name " << finding.assetId
                << "\n";
    assert(named);
  }
  // A schema the manifest forgets is the other half of the same rule.
  const AssetRegistry unschemaed =
      withAsset(registry, std::string(PaletteId),
                settingsOf(registry, std::string(PaletteId)), {});
  const TerrainAssetValidationReport schemaReport =
      validateTerrainAssets(request(unschemaed));
  expectFinding(schemaReport, TerrainAssetFindingKind::MissingDependency,
                "settings.schema");
  // Both role assets and the schema are missing from the empty dependency list.
  assert(ofKind(schemaReport, TerrainAssetFindingKind::MissingDependency)
             .size() == 3);
  // A texture the material document forgets is the same finding one level down,
  // because that document ships separately from the set that binds it.
  writeClay(R"json({
    "format_version": 1,
    "name": "Clay",
    "maps": [
      {"base_color": "asset://terrain/textures/clay_base"},
      {"normal": "asset://terrain/textures/clay_normal"}
    ]
  })json");
  const TerrainAssetValidationReport texture =
      validateTerrainAssets(request(registry));
  expectFinding(texture, TerrainAssetFindingKind::MissingDependency,
                "asset://terrain/textures/clay_normal");
  expectFinding(texture, TerrainAssetFindingKind::UnresolvedAsset,
                "maps/normal");
}

// An asset that may be used while developing but must not ship is exactly the
// distinction a requested use has to make.
void restrictedLicenceBlocksShippingOnly() {
  resetDocuments();
  const AssetRegistry restricted = withAsset(
      registry, std::string(GrassId),
      R"({"content_type":"surface","licence":{"spdx":"CC-BY-NC-4.0","redistributable":false}})",
      {std::string(GrassId)});

  const TerrainAssetValidationReport shipping =
      validateTerrainAssets(request(restricted, TerrainAssetUse::Shippable));
  assert(!shipping.shippable());
  assert(ofKind(shipping, TerrainAssetFindingKind::LicenceRestricted)
             .size() == 1);
  expectFinding(shipping, TerrainAssetFindingKind::LicenceRestricted,
                "CC-BY-NC-4.0");
  expectFinding(shipping, TerrainAssetFindingKind::LicenceRestricted,
                "must not ship");
  expectFinding(shipping, TerrainAssetFindingKind::LicenceRestricted,
                "Point the role at a redistributable asset");
  // One licence line for the asset, even though two documents bind it.
  assert(shipping.blockingCount() == 1);
  assert(shipping.advisoryCount() == 0);

  const TerrainAssetValidationReport development = validateTerrainAssets(
      request(restricted, TerrainAssetUse::DevelopmentOnly));
  // Still reported, because a development-only run is exactly when an author
  // wants to know the restriction exists before they ship.
  assert(ofKind(development, TerrainAssetFindingKind::LicenceRestricted)
             .size() == 1);
  assert(development.shippable());
  assert(development.blockingCount() == 0);
  assert(development.advisoryCount() == 1);
  assert(contains(development.summary(), "[advisory]"));
  assert(contains(development.summary(), "development-only"));

  // The manifest's own file field is spelled "license"; the declaration is read
  // under either spelling rather than letting a typo un-restrict an asset.
  const AssetRegistry spelled =
      withAsset(registry, std::string(GrassId),
                R"({"content_type":"surface","license":{"spdx":"CC-BY-NC-4.0","redistributable":false}})",
                {std::string(GrassId)});
  const TerrainAssetValidationReport alternate =
      validateTerrainAssets(request(spelled, TerrainAssetUse::Shippable));
  assert(ofKind(alternate, TerrainAssetFindingKind::LicenceRestricted).size() ==
         1);
  assert(!alternate.shippable());
}

// Silence is not permission. A manifest that declares nothing is reported as
// advice, never as a grant and never as a blocker.
void absentLicenceIsAdvisory() {
  resetDocuments();
  const AssetRegistry silent =
      withAsset(registry, std::string(RockId), R"({"content_type":"surface"})",
                {std::string(RockId)});
  for (const TerrainAssetUse use : {TerrainAssetUse::Shippable,
                                    TerrainAssetUse::DevelopmentOnly}) {
    const TerrainAssetValidationReport report =
        validateTerrainAssets(request(silent, use));
    const std::vector<TerrainAssetFinding> undeclared =
        ofKind(report, TerrainAssetFindingKind::UndeclaredLicence);
    assert(undeclared.size() == 1);
    assert(undeclared[0].assetId == RockId);
    // Restriction is unknown, not asserted, so nothing is blocked.
    assert(report.shippable());
    assert(report.blockingCount() == 0);
    assert(report.advisoryCount() == 1);
    expectFinding(report, TerrainAssetFindingKind::UndeclaredLicence,
                  "is not a licence grant");
    expectFinding(report, TerrainAssetFindingKind::UndeclaredLicence,
                  "developer-selected art");
    assert(contains(report.summary(), "[advisory] UndeclaredLicence"));
  }
  // A declaration that cannot be read is no more of a grant than none at all,
  // and its message has to say which field is wrong.
  const AssetRegistry malformed =
      withAsset(registry, std::string(RockId),
                R"({"content_type":"surface","licence":true})",
                {std::string(RockId)});
  const TerrainAssetValidationReport broken =
      validateTerrainAssets(request(malformed));
  assert(broken.shippable());
  expectFinding(broken, TerrainAssetFindingKind::UndeclaredLicence,
                "cannot be read");
  expectFinding(broken, TerrainAssetFindingKind::UndeclaredLicence,
                "redistributable");
  // A licence with no identifier is unidentified, not granted.
  const AssetRegistry unnamed =
      withAsset(registry, std::string(RockId),
                R"({"content_type":"surface","licence":{"redistributable":true}})",
                {std::string(RockId)});
  const TerrainAssetValidationReport unnamedReport =
      validateTerrainAssets(request(unnamed));
  assert(unnamedReport.shippable());
  expectFinding(unnamedReport, TerrainAssetFindingKind::UndeclaredLicence,
                "without naming a licence");
  // A manifest that records licence text says nothing about redistribution, and
  // the report has to point that out rather than treat the file as a grant.
  AssetRegistry withText = silent;
  for (AssetManifest &asset : withText.assets)
    if (asset.id == RockId)
      asset.licensePath = root / "LICENSE.txt";
  const TerrainAssetValidationReport text =
      validateTerrainAssets(request(withText));
  expectFinding(text, TerrainAssetFindingKind::UndeclaredLicence,
                "records licence text only");
}

// Colour-only materials are legal content, so the default request accepts them
// and only an explicit full-map request demands the rest.
void materialMapsAreOptionalUnlessRequired() {
  resetDocuments();
  assert(validateTerrainAssets(request(registry)).findings.empty());

  const TerrainAssetValidationReport strict = validateTerrainAssets(
      request(registry, TerrainAssetUse::Shippable, true));
  const std::vector<TerrainAssetFinding> missing =
      ofKind(strict, TerrainAssetFindingKind::MissingMap);
  // Albedo is there, so only the normal map is genuinely absent.
  assert(missing.size() == 1);
  assert(missing[0].assetId == ClayId);
  expectFinding(strict, TerrainAssetFindingKind::MissingMap, "no normal map");
  // Asked for and absent is a blocker: the renderer cannot shade as declared.
  assert(!strict.shippable());

  // A material with no maps at all is advisory under the default request: the
  // scalars are a deliberate choice, not a mistake.
  writeSet(R"json({
    "format_version": 1,
    "name": "Test set",
    "roles": {"rock": "asset://terrain/surfaces/flat"},
    "required_roles": ["rock"]
  })json");
  const TerrainAssetValidationReport flat =
      validateTerrainAssets(request(registry));
  assert(flat.shippable());
  assert(ofKind(flat, TerrainAssetFindingKind::MissingMap).size() == 1);
  expectFinding(flat, TerrainAssetFindingKind::MissingMap, "no maps at all");

  const TerrainAssetValidationReport flatStrict = validateTerrainAssets(
      request(registry, TerrainAssetUse::Shippable, true));
  assert(ofKind(flatStrict, TerrainAssetFindingKind::MissingMap).size() == 2);
  expectFinding(flatStrict, TerrainAssetFindingKind::MissingMap,
                "no base_color map");
  expectFinding(flatStrict, TerrainAssetFindingKind::MissingMap,
                "no normal map");
  assert(!flatStrict.shippable());

  // A map that does not resolve is a broken binding, not a missing one.
  writeSet(SetDocument);
  writeClay(R"json({
    "format_version": 1,
    "name": "Clay",
    "maps": [{"base_color": "asset://terrain/textures/absent"}]
  })json");
  const TerrainAssetValidationReport absent =
      validateTerrainAssets(request(registry));
  assert(!absent.shippable());
  expectFinding(absent, TerrainAssetFindingKind::UnresolvedAsset,
                "maps/base_color");
  // A slot the renderer samples as a texture has to hold a texture.
  writeClay(R"json({
    "format_version": 1,
    "name": "Clay",
    "maps": [{"base_color": "asset://audio/wind"}]
  })json");
  const TerrainAssetValidationReport wrong =
      validateTerrainAssets(request(registry));
  assert(!wrong.shippable());
  expectFinding(wrong, TerrainAssetFindingKind::WrongAssetType,
                "needs a texture");
  expectFinding(wrong, TerrainAssetFindingKind::WrongAssetType, "AudioClip");
  // A map slot the material loader does not know is that loader's rejection,
  // but the reference it carries still has to resolve.
  writeClay(R"json({
    "format_version": 1,
    "name": "Clay",
    "maps": [{"nomal": "asset://terrain/textures/absent"}]
  })json");
  const TerrainAssetValidationReport unknownSlot =
      validateTerrainAssets(request(registry));
  expectFinding(unknownSlot, TerrainAssetFindingKind::UnresolvedAsset,
                "maps/nomal");
}

// Art magnified or shrunk past what it was authored for is a real cost, and the
// report says how far the number went.
void scaleOutOfRangeIsReported() {
  resetDocuments();
  writePalette(R"json({
  "format_version": 2,
  "name": "Test meadow",
  "placements": {
    "grass": {
      "model": "asset://terrain/surfaces/grass",
      "weight": 1,
      "scale": [
        0.0001,
        400
      ]
    }
  }
})json");
  const TerrainAssetValidationReport report =
      validateTerrainAssets(request(registry));
  const std::vector<TerrainAssetFinding> scales =
      ofKind(report, TerrainAssetFindingKind::IncompatibleScale);
  // Both ends of the range are named, because only one of them may be wrong.
  assert(scales.size() == 2);
  assert(scales[0].role == "grass" && scales[1].role == "grass");
  assert(contains(scales[0].message, "0.0001"));
  assert(contains(scales[1].message, "400"));
  expectFinding(report, TerrainAssetFindingKind::IncompatibleScale, "0.001");
  expectFinding(report, TerrainAssetFindingKind::IncompatibleScale, "100");
  assert(!report.shippable());
  // Under a development-only run a scale problem is advice, not a blocker.
  const TerrainAssetValidationReport development = validateTerrainAssets(
      request(registry, TerrainAssetUse::DevelopmentOnly));
  assert(development.shippable());
  assert(ofKind(development, TerrainAssetFindingKind::IncompatibleScale)
             .size() == 2);

  // Material tiling is the same mistake in the other direction.
  writePalette(PaletteDocument);
  writeClay(R"json({
    "format_version": 1,
    "name": "Clay",
    "tiling": 500,
    "maps": [{"base_color": "asset://terrain/textures/clay_base"}]
  })json");
  const TerrainAssetValidationReport tiled =
      validateTerrainAssets(request(registry));
  assert(ofKind(tiled, TerrainAssetFindingKind::IncompatibleScale).size() == 1);
  expectFinding(tiled, TerrainAssetFindingKind::IncompatibleScale,
                "tiles 500 times");
  assert(!tiled.shippable());
}

// Two runs over the same project must produce the same report, and it must be
// sorted so a diff of two runs is readable.
void findingsAreDeterministicAndSorted() {
  // A clay document with no maps at all: advisory under the default request,
  // and it makes this report carry a MissingMap alongside everything else.
  writeClay(R"json({
    "format_version": 1,
    "name": "Clay",
    "tiling": 500
  })json");
  writeSet(R"json({
    "format_version": 1,
    "name": "Test set",
    "roles": {"rock": "asset://terrain/surfaces/clay", "sediment": "asset://data/notes"},
    "required_roles": ["rock", "snow"]
  })json");
  writePalette(R"json({
  "format_version": 2,
  "name": "Test meadow",
  "placements": {
    "grass": {
      "weight": 1,
      "scale": [
        0.0001,
        1
      ],
      "model": "asset://terrain/surfaces/grass"
    },
    "tree": {
      "weight": 1,
      "model": "asset://terrain/props/absent"
    },
    "soil": {
      "weight": 1,
      "model": "asset://audio/wind"
    }
  }
})json");
  const AssetRegistry messy =
      withAsset(registry, std::string(GrassId),
                R"({"content_type":"surface","licence":{"spdx":"CC-BY-NC-4.0","redistributable":false}})",
                {std::string(GrassId)});
  const TerrainAssetValidationRequest messyRequest = request(messy);
  const TerrainAssetValidationReport first =
      validateTerrainAssets(messyRequest);
  const TerrainAssetValidationReport second =
      validateTerrainAssets(messyRequest);
  assert(first.findings.size() == second.findings.size());
  assert(first.summary() == second.summary());
  assert(first.checked == second.checked);
  assert(first.findings.size() >= 8);
  assert(!first.shippable());

  // Sorted by kind, then role, then asset, so a diff lines up.
  for (std::size_t index = 1; index < first.findings.size(); ++index) {
    const TerrainAssetFinding &previous = first.findings[index - 1];
    const TerrainAssetFinding &current = first.findings[index];
    const bool ordered =
        static_cast<int>(previous.kind) < static_cast<int>(current.kind) ||
        (previous.kind == current.kind &&
         (previous.role < current.role ||
          (previous.role == current.role &&
           previous.assetId <= current.assetId)));
    if (!ordered)
      std::cerr << "findings are not sorted at " << index << "\n";
    assert(ordered);
  }
  // Every kind the messy project can produce is actually represented, so this
  // case fails if the validator ever stops reporting one of them.
  for (const TerrainAssetFindingKind kind :
       {TerrainAssetFindingKind::MissingRole,
        TerrainAssetFindingKind::UnresolvedAsset,
        TerrainAssetFindingKind::WrongAssetType,
        TerrainAssetFindingKind::MissingMap,
        TerrainAssetFindingKind::IncompatibleScale,
        TerrainAssetFindingKind::LicenceRestricted,
        TerrainAssetFindingKind::MissingDependency}) {
    const bool present = std::ranges::any_of(
        first.findings, [&](const TerrainAssetFinding &item) {
          return item.kind == kind;
        });
    if (!present)
      std::cerr << "expected a " << terrainAssetFindingKindName(kind)
                << " finding\n";
    assert(present);
  }
}

// A request the validator cannot act on is a programming mistake, not an
// authoring finding, and it says which half is missing.
void rejectsImpossibleRequests() {
  try {
    TerrainAssetValidationRequest nullRegistry;
    nullRegistry.paletteId = PaletteId;
    (void)validateTerrainAssets(nullRegistry);
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(contains(failure.what(), "requires an asset registry"));
  }
  try {
    TerrainAssetValidationRequest empty;
    empty.registry = &registry;
    (void)validateTerrainAssets(empty);
    assert(false);
  } catch (const std::invalid_argument &failure) {
    assert(contains(failure.what(), "needs a palette or a material set"));
  }
  // A request may name only one of the two documents.
  resetDocuments();
  TerrainAssetValidationRequest paletteOnly;
  paletteOnly.registry = &registry;
  paletteOnly.paletteId = PaletteId;
  assert(validateTerrainAssets(paletteOnly).findings.empty());
  TerrainAssetValidationRequest setOnly;
  setOnly.registry = &registry;
  setOnly.materialSetId = SetId;
  assert(validateTerrainAssets(setOnly).findings.empty());
}

// The CLI and the editor panel print the same summary, so it has to name every
// finding and say whether the requested use is blocked.
void summaryNamesEveryFinding() {
  resetDocuments();
  writePalette(R"json({
  "format_version": 2,
  "name": "Test meadow",
  "placements": {
    "grass": {
      "weight": 1,
      "scale": [
        1,
        900
      ],
      "model": "asset://terrain/surfaces/grass"
    },
    "soil": {
      "weight": 1,
      "model": "asset://terrain/surfaces/absent"
    }
  }
})json");
  const AssetRegistry restricted =
      withAsset(registry, std::string(GrassId),
                R"({"content_type":"surface","licence":{"spdx":"CC-BY-NC-4.0","redistributable":false}})",
                {std::string(GrassId)});
  const TerrainAssetValidationReport report =
      validateTerrainAssets(request(restricted));
  assert(report.findings.size() >= 4);
  const std::string text = report.summary();
  for (const TerrainAssetFinding &finding : report.findings) {
    assert(contains(
        text, std::string(terrainAssetFindingKindName(finding.kind))));
    assert(contains(text, finding.message));
    if (!finding.role.empty())
      assert(contains(text, finding.role));
    if (!finding.assetId.empty())
      assert(contains(text, finding.assetId));
    assert(
        contains(text, report.blocking(finding) ? "[blocking]" : "[advisory]"));
  }
  // The single summary line counts the same numbers the accessors report.
  assert(contains(text, std::to_string(report.findings.size()) + " findings, " +
                           std::to_string(report.blockingCount()) +
                           " blocking, " +
                           std::to_string(report.advisoryCount()) +
                           " advisory"));
  assert(contains(text, "blocked"));
  assert(report.blockingCount() + report.advisoryCount() ==
         report.findings.size());
}

// The checked-in example is the content this validator has to stay honest
// about: placeholder materials and no licence declarations anywhere, which is
// advice rather than a shipping blocker.
void exampleProjectIsAdvisoryOnly() {
  const AssetRegistry example =
      loadAuthoredAssetRegistry(DEMI_SOURCE_DIR "/examples/terrain_3d");
  TerrainAssetValidationRequest exampleRequest;
  exampleRequest.registry = &example;
  exampleRequest.paletteId = "asset://terrain/palettes/meadow";
  exampleRequest.materialSetId = "asset://terrain/materials/terrain_pbr";
  const TerrainAssetValidationReport report =
      validateTerrainAssets(exampleRequest);
  if (!report.shippable())
    std::cerr << report.summary() << "\n";
  assert(report.shippable());
  assert(report.checked >= 8);
  // Every art asset in the example is unlicensed, so every finding is advice.
  for (const TerrainAssetFinding &finding : report.findings) {
    assert(finding.kind == TerrainAssetFindingKind::UndeclaredLicence);
    assert(contains(finding.assetId, "asset://terrain/surfaces/"));
    assert(!report.blocking(finding));
  }
  // Demanding every map is a separate question, and the example answers it: its
  // roles bind render Materials, which carry no slot maps at all.
  exampleRequest.requireFullMaterialMaps = true;
  const TerrainAssetValidationReport strict =
      validateTerrainAssets(exampleRequest);
  if (!strict.shippable())
    std::cerr << strict.summary() << "\n";
  assert(strict.shippable());
}

} // namespace

int main() {
  root = std::filesystem::temp_directory_path() /
         "demi_terrain_asset_validation_tests";
  std::error_code filesystemError;
  // Removed up front as well as on success, so a previous failed run cannot
  // leave a tree behind that changes what a finding proves.
  std::filesystem::remove_all(root, filesystemError);
  std::filesystem::create_directories(root, filesystemError);
  if (!write(
          root / "palette_schema.json",
          R"({"format_version":1,"type":"object","required":["format_version","name"],"properties":{"format_version":{"type":"integer","enum":[2]},"name":{"type":"string"},"placements":{"type":"object"},"material_set":{"type":"string","reference":"asset"}})") ||
      !write(root / "set_schema.json", R"({
        "format_version": 1,
        "type": "object",
        "required": ["format_version", "name", "roles"],
        "properties": {
          "format_version": {"type": "integer", "enum": [1]},
          "name": {"type": "string"},
          "roles": {
            "type": "object",
            "properties": {
              "rock": {"type": "string", "reference": "asset"},
              "ground": {"type": "string", "reference": "asset"},
              "sediment": {"type": "string", "reference": "asset"}
            }
          },
          "required_roles": {"type": "array", "items": {"type": "string"}}
        }
      })") ||
      !write(root / "grass.material.json",
             R"({"format_version":1,"shader":"builtin://lit"})") ||
      !write(root / "rock.material.json",
             R"({"format_version":1,"shader":"builtin://lit"})") ||
      !write(root / "clay_base.png", "texture placeholder") ||
      !write(root / "oak.glb", "glTF placeholder") ||
      !write(root / "wind.audio.json", R"({"format_version":1})") ||
      !write(root / "notes.json", R"({"format_version":1,"note":"x"})") ||
      !write(root / "LICENSE.txt", "Attribution text only.") ||
      // The documents themselves are hashed by buildRegistry, so they exist
      // before the manifests that point at them.
      !write(root / "palette.json", PaletteDocument) ||
      !write(root / "set.json", SetDocument) ||
      !write(root / "clay.json", ClayDocument) ||
      !write(root / "flat.json", FlatDocument)) {
    std::cerr << "Could not create terrain asset validation fixtures.\n";
    return 1;
  }
  registry = buildRegistry();
  resetDocuments();

  cleanProjectIsShippable();
  unboundRequiredRoleIsReported();
  unresolvedReferenceBlocks();
  wrongAssetTypeBlocks();
  missingDependencyIsDetectedAndAgrees();
  restrictedLicenceBlocksShippingOnly();
  absentLicenceIsAdvisory();
  materialMapsAreOptionalUnlessRequired();
  scaleOutOfRangeIsReported();
  findingsAreDeterministicAndSorted();
  rejectsImpossibleRequests();
  summaryNamesEveryFinding();
  exampleProjectIsAdvisoryOnly();

  std::filesystem::remove_all(root, filesystemError);
  std::cout << "Terrain asset validation checks passed\n";
  return 0;
}
