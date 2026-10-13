#include "demi/assets/TerrainAssetValidation.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/DataDocument.h"
#include "demi/assets/MaterialAsset.h"
#include "demi/diagnostics/Diagnostic.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace demi::assets {
namespace {

using Json = nlohmann::json;

constexpr std::string_view AssetPrefix = "asset://";
constexpr std::string_view PaletteKind = "terrain palette";
constexpr std::string_view MaterialSetKind = "terrain material set";
constexpr std::string_view MaterialKind = "terrain material";
constexpr std::string_view PaletteContentType = "terrain_palette";
constexpr std::string_view MaterialSetContentType = "terrain_material_set";
constexpr std::string_view MaterialContentType = "terrain_material";

// A sane range for authored terrain art. Below the floor a surface or prop
// collapses into an unreadable speck; above the ceiling the authored detail is
// magnified past anything the texture or mesh was authored for, and the result
// is either a blurred blob or a floating object the size of a district.
constexpr float MinimumTiling = 0.01F;
constexpr float MaximumTiling = 100.F;
constexpr float MinimumScale = 0.001F;
constexpr float MaximumScale = 100.F;

struct KindName {
  TerrainAssetFindingKind kind;
  std::string_view name;
};

constexpr KindName KindNames[]{
    {TerrainAssetFindingKind::MissingRole, "MissingRole"},
    {TerrainAssetFindingKind::UnresolvedAsset, "UnresolvedAsset"},
    {TerrainAssetFindingKind::WrongAssetType, "WrongAssetType"},
    {TerrainAssetFindingKind::MissingMap, "MissingMap"},
    {TerrainAssetFindingKind::IncompatibleScale, "IncompatibleScale"},
    {TerrainAssetFindingKind::LicenceRestricted, "LicenceRestricted"},
    {TerrainAssetFindingKind::MissingDependency, "MissingDependency"},
    {TerrainAssetFindingKind::UndeclaredLicence, "UndeclaredLicence"},
};

// Whole numbers print as integers; anything else keeps enough precision to be
// acted on. data_read::readable would print 0.001 as "0.00", which makes the
// scale floor unreadable in the very message that names it.
std::string number(const float value) {
  if (std::isfinite(value) && value == std::floor(value) &&
      std::abs(value) < 1e6F)
    return std::to_string(static_cast<long long>(value));
  std::ostringstream text;
  text << std::setprecision(6) << value;
  return text.str();
}

std::string stringValue(const DataValue *value) {
  if (value == nullptr || !value->isString())
    return {};
  return std::get<std::string>(value->value);
}

// A quiet NaN rather than a fallback: an absent or non-numeric field has no
// value to check, and the document layer already rejects that shape.
float numberValue(const DataValue *value) {
  if (value == nullptr || !value->isNumber())
    return std::numeric_limits<float>::quiet_NaN();
  return value->isInteger()
             ? static_cast<float>(std::get<std::int64_t>(value->value))
             : static_cast<float>(std::get<double>(value->value));
}

std::string summarize(const Diagnostics &diagnostics) {
  std::string summary;
  for (const Diagnostic &item : diagnostics) {
    if (item.severity != Severity::Error)
      continue;
    if (!summary.empty())
      summary += "; ";
    summary += item.code + ": " + item.message;
  }
  return summary.empty() ? "unknown reason" : summary;
}

// What the author needs to know about the manifest that resolved: a DataAsset
// is named by its content type, because "is DataAsset" on its own does not say
// which document kind was bound.
std::string describeType(const AssetManifest &manifest) {
  if (manifest.type != "DataAsset")
    return manifest.type;
  const std::optional<DataAssetMetadata> metadata = dataAssetMetadata(manifest);
  const std::string contentType =
      metadata.has_value() && !metadata->contentType.empty()
          ? metadata->contentType
          : std::string("<no content_type>");
  return "a DataAsset declaring settings.content_type \"" + contentType + "\"";
}

struct LicenceState {
  enum class Kind {
    Absent,       // nothing declared at all
    Malformed,    // declared, but not a readable declaration
    Unidentified, // declared shippable, without naming a licence
    Development,  // declared not redistributable
    Shippable,    // declared redistributable under a named licence
  };
  Kind kind = Kind::Absent;
  std::string detail;
};

// An explicit declaration in the manifest's settings: {"licence": {"spdx":
// "CC-BY-4.0", "redistributable": false}}. This classifies art that is already
// in the project; it never fetches, downloads or installs anything.
LicenceState readLicence(const AssetManifest &manifest) {
  LicenceState result;
  const Json settings = Json::parse(manifest.settingsJson, nullptr, false);
  if (!settings.is_object())
    return result;
  // Both spellings are accepted because the manifest's own file field is
  // "license" while the plan and its docs say "licence"; a typo here must not
  // silently un-restrict an asset.
  auto declaration = settings.find("licence");
  if (declaration == settings.end())
    declaration = settings.find("license");
  if (declaration == settings.end() || declaration->is_null())
    return result;
  if (!declaration->is_object()) {
    result.kind = LicenceState::Kind::Malformed;
    result.detail = "settings.licence must be an object with \"spdx\" and "
                    "\"redistributable\"";
    return result;
  }
  const auto redistributable = declaration->find("redistributable");
  if (redistributable == declaration->end() || !redistributable->is_boolean()) {
    result.kind = LicenceState::Kind::Malformed;
    result.detail = "settings.licence.redistributable must be true or false";
    return result;
  }
  const auto spdx = declaration->find("spdx");
  const std::string identifier = spdx != declaration->end() && spdx->is_string()
                                     ? spdx->get<std::string>()
                                     : std::string();
  if (!(*redistributable)) {
    result.kind = LicenceState::Kind::Development;
    result.detail = identifier;
    return result;
  }
  if (identifier.empty()) {
    result.kind = LicenceState::Kind::Unidentified;
    return result;
  }
  result.kind = LicenceState::Kind::Shippable;
  return result;
}

// One licence finding per asset, not per role that binds it: nine surface roles
// sharing one texture must not produce nine identical lines.
class Findings {
public:
  Findings(const AssetRegistry &registry, const bool requireFullMaterialMaps)
      : registry_(registry),
        requireFullMaterialMaps_(requireFullMaterialMaps) {}

  [[nodiscard]] const AssetRegistry &registry() const noexcept {
    return registry_;
  }
  [[nodiscard]] bool fullMaps() const noexcept {
    return requireFullMaterialMaps_;
  }

  void countReference() noexcept { ++checked_; }
  [[nodiscard]] std::size_t checked() const noexcept { return checked_; }

  void add(const TerrainAssetFindingKind kind, std::string role,
           std::string assetId, std::string message) {
    findings_.push_back({.kind = kind,
                         .role = std::move(role),
                         .assetId = std::move(assetId),
                         .message = std::move(message)});
  }

  bool firstLicenceFinding(const std::string &assetId) {
    return licences_.insert(assetId).second;
  }

  [[nodiscard]] std::vector<TerrainAssetFinding> take() {
    return std::move(findings_);
  }

private:
  const AssetRegistry &registry_;
  bool requireFullMaterialMaps_;
  std::size_t checked_ = 0;
  std::vector<TerrainAssetFinding> findings_;
  std::set<std::string, std::less<>> licences_;
};

struct DocumentRead {
  const AssetManifest *manifest = nullptr;
  std::shared_ptr<const DataDocument> document;
};

// Reads a DataAsset document of one content type for inspection. Every refusal
// is a finding, never an exception: this validator exists to list every problem
// at once instead of stopping at the first one.
DocumentRead readDocument(const AssetRegistry &registry,
                          const std::string &assetId,
                          const std::string_view kind,
                          const std::string_view contentType,
                          const std::string &role, Findings &findings) {
  DocumentRead result;
  if (!assetId.starts_with(AssetPrefix)) {
    findings.add(TerrainAssetFindingKind::UnresolvedAsset, role, assetId,
                 std::string(kind) + " id \"" + assetId +
                     "\" is not an asset:// reference. Use the asset id, for "
                     "example \"asset://terrain/palettes/meadow\".");
    return result;
  }
  const AssetManifest *manifest = findAsset(registry, assetId);
  if (manifest == nullptr) {
    findings.add(TerrainAssetFindingKind::UnresolvedAsset, role, assetId,
                 std::string(kind) + " " + assetId +
                     " does not resolve. Add the asset manifest for that id or "
                     "fix the reference.");
    return result;
  }
  result.manifest = manifest;
  if (manifest->type != "DataAsset") {
    findings.add(TerrainAssetFindingKind::WrongAssetType, role, assetId,
                 std::string(kind) + " " + assetId +
                     " must be a DataAsset, but is " + manifest->type +
                     ". Point the binding at a DataAsset.");
    return result;
  }
  const std::optional<DataAssetMetadata> metadata =
      dataAssetMetadata(*manifest);
  if (!metadata.has_value() || metadata->contentType != contentType) {
    findings.add(TerrainAssetFindingKind::WrongAssetType, role, assetId,
                 std::string(kind) + " " + assetId + " must declare "
                     "settings.content_type \"" + std::string(contentType) +
                     "\", but declares \"" +
                     (metadata.has_value() ? metadata->contentType
                                           : std::string("<unreadable>")) +
                     "\".");
    return result;
  }
  Diagnostics diagnostics;
  const auto loaded = loadDataAsset(*manifest, {}, &diagnostics);
  if (!loaded.has_value()) {
    findings.add(TerrainAssetFindingKind::UnresolvedAsset, role, assetId,
                 std::string(kind) + " " + assetId +
                     " could not be read: " + summarize(diagnostics));
    return result;
  }
  result.document = loaded->document;
  return result;
}

bool declaresContentType(const AssetManifest &manifest,
                         const std::string_view contentType) {
  if (manifest.type != "DataAsset")
    return false;
  const std::optional<DataAssetMetadata> metadata = dataAssetMetadata(manifest);
  return metadata.has_value() && metadata->contentType == contentType;
}

void inspectLicence(Findings &findings, const std::string &role,
                    const std::string &assetId, const AssetManifest &manifest) {
  if (!findings.firstLicenceFinding(assetId))
    return;
  const LicenceState licence = readLicence(manifest);
  const std::string subject = "asset " + assetId;
  switch (licence.kind) {
  case LicenceState::Kind::Absent:
    findings.add(TerrainAssetFindingKind::UndeclaredLicence, role, assetId,
                 subject +
                     " declares no licence, and an absent declaration is not a "
                     "licence grant. settings.licence.spdx and "
                     "settings.licence.redistributable say which licence "
                     "applies and whether the art may ship; without them the "
                     "asset is unrestricted by default and its redistribution "
                     "terms are unknown. Declare them, or replace the binding "
                     "with developer-selected art you have licensed." +
                     (manifest.licensePath
                          ? " The manifest license file records licence text "
                            "only; it does not declare redistribution terms."
                          : ""));
    return;
  case LicenceState::Kind::Malformed:
    findings.add(TerrainAssetFindingKind::UndeclaredLicence, role, assetId,
                 subject + " declares a licence that cannot be read: " +
                     licence.detail +
                     ". Use {\"licence\": {\"spdx\": \"CC-BY-4.0\", "
                     "\"redistributable\": true}} or remove the field; an "
                     "unreadable declaration is not a licence grant.");
    return;
  case LicenceState::Kind::Unidentified:
    findings.add(TerrainAssetFindingKind::UndeclaredLicence, role, assetId,
                 subject +
                     " declares settings.licence.redistributable true without "
                     "naming a licence, so the terms are unidentified. Add the "
                     "SPDX identifier, for example \"spdx\": \"CC0-1.0\".");
    return;
  case LicenceState::Kind::Development:
    findings.add(TerrainAssetFindingKind::LicenceRestricted, role, assetId,
                 subject + " declares SPDX " + licence.detail +
                     " with redistributable false, so it may be used while "
                     "developing but must not ship. Point the role at a "
                     "redistributable asset, or keep the project "
                     "development-only and leave the declaration honest.");
    return;
  case LicenceState::Kind::Shippable:
    return;
  }
}

void inspectDeclaredDependencies(
    Findings &findings, const std::string &documentId, const std::string &role,
    const AssetManifest &manifest,
    const std::set<std::string, std::less<>> &references) {
  const std::optional<DataAssetMetadata> metadata = dataAssetMetadata(manifest);
  if (metadata.has_value() && !metadata->schema.empty() &&
      std::ranges::find(manifest.dependencies, metadata->schema) ==
          manifest.dependencies.end())
    findings.add(TerrainAssetFindingKind::MissingDependency, role, documentId,
                 "document " + documentId +
                     " declares settings.schema " + metadata->schema +
                     " but does not list it in dependencies. Add it, or "
                     "demi validate will fail it as an undeclared dependency.");
  // Only asset:// references, exactly as validateDataAssets enforces, so the
  // two cannot disagree about what has to be declared. A prefab:// reference is
  // resolved by the prefab layer, not by the manifest dependency array.
  for (const std::string &reference : references)
    if (std::ranges::find(manifest.dependencies, reference) ==
        manifest.dependencies.end())
      findings.add(TerrainAssetFindingKind::MissingDependency, role, reference,
                   "document " + documentId + " references " + reference +
                       " but does not declare it in dependencies. Cook follows "
                       "the manifest, so " + reference +
                       " would be left out of the package; add it to the "
                       "dependencies array.");
}

void inspectScale(Findings &findings, const std::string &documentId,
                  const std::string &role, const std::string &assetId,
                  const DataValue &entry) {
  const DataValue *scale = entry.find("scale");
  const DataValue::Array *range = scale != nullptr ? scale->array() : nullptr;
  if (range == nullptr || range->size() != 2)
    return; // An inverted or misshapen range is the palette loader's rejection.
  const float minimum = numberValue(&(*range)[0]);
  const float maximum = numberValue(&(*range)[1]);
  if (std::isnan(minimum) || std::isnan(maximum))
    return;
  // Each end is named separately: only one of them may be the mistake, and an
  // author has to know which one to retype.
  const auto outOfRange = [&](const float value, const std::string_view bound) {
    findings.add(TerrainAssetFindingKind::IncompatibleScale, role, assetId,
                 "terrain palette " + documentId + " role \"" + role +
                     "\" scales its art to " + number(value) + " (" +
                     std::string(bound) + "), outside the range " +
                     number(MinimumScale) + " to " + number(MaximumScale) +
                     ". Authored terrain art is not built to be shrunk to an "
                     "unreadable speck or magnified past its detail.");
  };
  if (minimum < MinimumScale || minimum > MaximumScale)
    outOfRange(minimum, "scale_min");
  if (maximum < MinimumScale || maximum > MaximumScale)
    outOfRange(maximum, "scale_max");
}

// A role without a prefab feeds its asset to MeshRenderer.model, so it needs
// geometry. A prefab may supply geometry while its asset names material art.
void inspectPaletteTarget(Findings &findings, const std::string &paletteId,
                          const std::string &role, const std::string &assetId,
                          const DataValue &entry) {
  const AssetManifest *target = findAsset(findings.registry(), assetId);
  if (target == nullptr) {
    findings.add(TerrainAssetFindingKind::UnresolvedAsset, role, assetId,
                 "terrain palette " + paletteId + " role \"" + role +
                     "\" binds " + assetId +
                     ", which does not resolve. Add the manifest for that id or "
                     "point the role at existing art.");
    return;
  }
  if (target->type != "Model3D")
    findings.add(TerrainAssetFindingKind::WrongAssetType, role, assetId,
                 "terrain palette " + paletteId + " role \"" + role +
                     "\" needs Model3D geometry; " + assetId + " is " +
                     describeType(*target) +
                     ". Point the role at a model or add a geometry prefab.");
  inspectScale(findings, paletteId, role, assetId, entry);
  inspectLicence(findings, role, assetId, *target);
}

void inspectTerrainMap(Findings &findings, const std::string &role,
                       const std::string &materialId,
                       const std::string &slot, const std::string &reference) {
  const std::string where = "maps/" + slot;
  if (!reference.starts_with(AssetPrefix)) {
    findings.add(TerrainAssetFindingKind::UnresolvedAsset, role, reference,
                 "terrain material " + materialId + " " + where +
                     " must be an asset:// reference, but is \"" + reference +
                     "\".");
    return;
  }
  const AssetManifest *texture = findAsset(findings.registry(), reference);
  if (texture == nullptr) {
    findings.add(TerrainAssetFindingKind::UnresolvedAsset, role, reference,
                 "terrain material " + materialId + " " + where +
                     " references " + reference +
                     ", which does not resolve. Add the texture manifest or "
                     "fix the reference.");
    return;
  }
  if (texture->type != "Texture2D")
    findings.add(TerrainAssetFindingKind::WrongAssetType, role, reference,
                 "terrain material " + materialId + " " + where + " needs a "
                 "texture, but " + reference + " is " + describeType(*texture) +
                     ". Point the slot at a Texture2D asset.");
  inspectLicence(findings, role, reference, *texture);
}

void inspectTerrainMaterial(Findings &findings, const std::string &setId,
                            const std::string &role,
                            const std::string &materialId,
                            const AssetManifest &manifest,
                            const DataValue &root) {
  const float tiling = numberValue(root.find("tiling"));
  if (!std::isnan(tiling) && (tiling < MinimumTiling || tiling > MaximumTiling))
    findings.add(TerrainAssetFindingKind::IncompatibleScale, role, materialId,
                 "terrain material set " + setId + " role \"" + role +
                     "\" binds " + materialId + ", which tiles " +
                     number(tiling) + " times per authored surface unit, "
                     "outside the range " + number(MinimumTiling) + " to " +
                     number(MaximumTiling) +
                     ". Tiling below that collapses the projection and tiling "
                     "above it magnifies the texels past what the texture was "
                     "authored for.");

  std::map<TerrainMaterialMapSlot, std::string> maps;
  std::set<std::string, std::less<>> references;
  if (const DataValue *entries = root.find("maps");
      entries != nullptr && entries->isArray()) {
    for (const DataValue &entry : *entries->array()) {
      const DataValue::Object *slots = entry.object();
      // An unknown slot is the material loader's rejection, but its reference
      // still has to resolve and be declared.
      if (slots == nullptr || slots->size() != 1)
        continue;
      const auto [name, value] = *slots->begin();
      const std::string reference = stringValue(&value);
      if (reference.empty())
        continue;
      references.insert(reference);
      findings.countReference();
      if (const auto slot = terrainMaterialMapSlotFromName(name);
          slot.has_value())
        maps.emplace(*slot, reference);
      inspectTerrainMap(findings, role, materialId, name, reference);
    }
  }
  inspectDeclaredDependencies(findings, materialId, role, manifest, references);

  if (findings.fullMaps()) {
    if (!maps.contains(TerrainMaterialMapSlot::BaseColor))
      findings.add(TerrainAssetFindingKind::MissingMap, role, materialId,
                   "terrain material set " + setId + " role \"" + role +
                       "\" binds " + materialId +
                       ", which has no base_color map. Assign one, for example "
                       "{\"base_color\": \"asset://terrain/textures/rock\"}; a "
                       "terrain surface with no colour map is a flat tint.");
    if (!maps.contains(TerrainMaterialMapSlot::Normal))
      findings.add(TerrainAssetFindingKind::MissingMap, role, materialId,
                   "terrain material set " + setId + " role \"" + role +
                       "\" binds " + materialId +
                       ", which has no normal map. Assign one, for example "
                       "{\"normal\": \"asset://terrain/textures/rock_normal\"}, "
                       "or drop requireFullMaterialMaps when the renderer can "
                       "shade this surface without one.");
  } else if (maps.empty())
    findings.add(TerrainAssetFindingKind::MissingMap, role, materialId,
                 "terrain material set " + setId + " role \"" + role +
                     "\" binds " + materialId +
                     ", which declares no maps at all, so the surface is a flat "
                     "scalar tint. Assign a base_color map, or accept the flat "
                     "surface and leave maps unrequired.");
}

// A material role binds a material and nothing else, matching the rule the
// material set already enforces when it loads.
void inspectMaterialSetTarget(Findings &findings, const std::string &setId,
                              const std::string &role,
                              const std::string &assetId) {
  const AssetManifest *target = findAsset(findings.registry(), assetId);
  if (target == nullptr) {
    findings.add(TerrainAssetFindingKind::UnresolvedAsset, role, assetId,
                 "terrain material set " + setId + " role \"" + role +
                     "\" binds " + assetId +
                     ", which does not resolve. Add the manifest for that id or "
                     "point the role at existing art.");
    return;
  }
  if (target->type != "Material" &&
      !declaresContentType(*target, MaterialContentType)) {
    // The licence of something that is not a surface material is not worth a
    // line: replacing the binding is the only actionable advice.
    findings.add(TerrainAssetFindingKind::WrongAssetType, role, assetId,
                 "terrain material set " + setId + " role \"" + role +
                     "\" needs a material: a Material asset, or a DataAsset "
                     "declaring settings.content_type \"terrain_material\", but " +
                     assetId + " is " + describeType(*target) +
                     ". Point the role at a material asset.");
    return;
  }
  inspectLicence(findings, role, assetId, *target);
  if (!declaresContentType(*target, MaterialContentType))
    return; // A render Material carries its maps inside its own document.
  const DocumentRead material = readDocument(findings.registry(), assetId,
                                             MaterialKind, MaterialContentType,
                                             role, findings);
  if (!material.document)
    return;
  inspectTerrainMaterial(findings, setId, role, assetId, *material.manifest,
                         material.document->root());
}

void inspectMaterialSet(Findings &findings, const std::string &setId);

void inspectPalette(Findings &findings, const std::string &paletteId) {
  const auto palette = readDocument(findings.registry(), paletteId, PaletteKind,
                                    PaletteContentType, {}, findings);
  if (!palette.document)
    return;
  findings.countReference();
  const auto &root = palette.document->root();
  std::set<std::string, std::less<>> references;
  const auto materialSet = stringValue(root.find("material_set"));
  if (!materialSet.empty()) {
    references.insert(materialSet);
    inspectMaterialSet(findings, materialSet);
  }
  const auto *placements = root.find("placements");
  if (placements && !placements->isObject()) {
    findings.add(
        TerrainAssetFindingKind::UnresolvedAsset, {}, paletteId,
        "terrain palette placements must be an object keyed by rule ID.");
    return;
  }
  if (placements) {
    for (const auto &[ruleId, entry] : *placements->object()) {
      const auto model = stringValue(entry.find("model"));
      const auto prefab = stringValue(entry.find("prefab"));
      if (model.empty()) {
        if (prefab.empty())
          findings.add(TerrainAssetFindingKind::UnresolvedAsset, ruleId, {},
                       "placements/" + ruleId +
                           "/model requires model or prefab.");
        else
          inspectScale(findings, paletteId, ruleId, prefab, entry);
        continue;
      }
      if (!model.starts_with(AssetPrefix)) {
        findings.add(TerrainAssetFindingKind::UnresolvedAsset, ruleId, model,
                     "placements/" + ruleId +
                         "/model must be an asset:// reference.");
        continue;
      }
      references.insert(model);
      findings.countReference();
      inspectPaletteTarget(findings, paletteId, ruleId, model, entry);
    }
  }
  inspectDeclaredDependencies(findings, paletteId, {}, *palette.manifest,
                              references);
}

void inspectMaterialSet(Findings &findings, const std::string &setId) {
  const DocumentRead set = readDocument(findings.registry(), setId,
                                        MaterialSetKind, MaterialSetContentType,
                                        {}, findings);
  if (!set.document)
    return;
  findings.countReference();
  const DataValue &root = set.document->root();
  const DataValue *roles = root.find("roles");
  const DataValue::Object *roleMap =
      roles != nullptr ? roles->object() : nullptr;
  if (roleMap == nullptr) {
    findings.add(TerrainAssetFindingKind::UnresolvedAsset, {}, setId,
                 "terrain material set " + setId +
                     " cannot be inspected: \"roles\" must be an object keyed "
                     "by role name, so no role could be checked. Fix the "
                     "document; demi validate reports the shape error.");
    return;
  }

  std::set<std::string, std::less<>> references;
  for (const auto &[roleName, value] : *roleMap) {
    // An unknown role name is the set loader's rejection, but the reference it
    // binds still has to resolve and be declared.
    const std::string reference = stringValue(&value);
    if (reference.empty()) {
      findings.add(TerrainAssetFindingKind::UnresolvedAsset, roleName, {},
                   "terrain material set " + setId + " role \"" + roleName +
                       "\" binds no usable asset:// reference. Point roles/" +
                       roleName + " at an existing material asset.");
      continue;
    }
    if (!reference.starts_with(AssetPrefix)) {
      findings.add(TerrainAssetFindingKind::UnresolvedAsset, roleName,
                   reference, "terrain material set " + setId + " roles/" +
                                   roleName +
                       " must be an asset:// reference, but is \"" + reference +
                       "\". Use the asset id of an existing material.");
      continue;
    }
    references.insert(reference);
    findings.countReference();
    inspectMaterialSetTarget(findings, setId, roleName, reference);
  }

  if (const DataValue *required = root.find("required_roles");
      required != nullptr && required->isArray())
    for (const DataValue &roleName : *required->array()) {
      const std::string name = stringValue(&roleName);
      if (name.empty() || roleMap->contains(name))
        continue;
      findings.add(TerrainAssetFindingKind::MissingRole, name, {},
                   "terrain material set " + setId + " requires role \"" + name +
                       "\", which it does not bind, so generation would leave "
                       "that surface unshaded. Add roles/" + name + " or drop "
                       "it from required_roles.");
    }

  inspectDeclaredDependencies(findings, setId, {}, *set.manifest, references);
}

} // namespace

std::string_view
terrainAssetFindingKindName(const TerrainAssetFindingKind kind) {
  for (const KindName &entry : KindNames)
    if (entry.kind == kind)
      return entry.name;
  return "UnresolvedAsset";
}

std::string_view terrainAssetUseName(const TerrainAssetUse use) {
  switch (use) {
  case TerrainAssetUse::DevelopmentOnly:
    return "development-only";
  case TerrainAssetUse::Shippable:
    return "shippable";
  }
  return "shippable";
}

bool TerrainAssetValidationReport::blocking(
    const TerrainAssetFinding &finding) const noexcept {
  switch (finding.kind) {
  case TerrainAssetFindingKind::MissingRole:
  case TerrainAssetFindingKind::UnresolvedAsset:
  case TerrainAssetFindingKind::WrongAssetType:
  case TerrainAssetFindingKind::MissingDependency:
    return true;
  // A development-only project is not blocked by a shipping restriction it will
  // never hit, and an incomplete material is only a blocker when the caller
  // said the renderer needs every map.
  case TerrainAssetFindingKind::LicenceRestricted:
  case TerrainAssetFindingKind::IncompatibleScale:
    return use == TerrainAssetUse::Shippable;
  case TerrainAssetFindingKind::MissingMap:
    return requireFullMaterialMaps;
  // Nothing declared is unknown, not restricted: it is advice, and advice does
  // not block a build the author asked to inspect.
  case TerrainAssetFindingKind::UndeclaredLicence:
    return false;
  }
  return true;
}

std::size_t TerrainAssetValidationReport::blockingCount() const noexcept {
  return static_cast<std::size_t>(
      std::ranges::count_if(findings, [&](const TerrainAssetFinding &finding) {
        return blocking(finding);
      }));
}

std::size_t TerrainAssetValidationReport::advisoryCount() const noexcept {
  return findings.size() - blockingCount();
}

bool TerrainAssetValidationReport::shippable() const noexcept {
  return blockingCount() == 0;
}

std::string TerrainAssetValidationReport::summary() const {
  std::string text;
  for (const TerrainAssetFinding &finding : findings) {
    text += "  [";
    text += blocking(finding) ? "blocking" : "advisory";
    text += "] ";
    text += terrainAssetFindingKindName(finding.kind);
    text += " role=";
    text += finding.role.empty() ? "-" : finding.role;
    text += " asset=";
    text += finding.assetId.empty() ? "-" : finding.assetId;
    text += ": ";
    text += finding.message;
    text += '\n';
  }
  text += "terrain asset validation (" + std::string(terrainAssetUseName(use)) +
          "): " + std::to_string(findings.size()) +
          (findings.size() == 1 ? " finding, " : " findings, ") +
          std::to_string(blockingCount()) + " blocking, " +
          std::to_string(advisoryCount()) + " advisory, " +
          std::to_string(checked) +
          (checked == 1 ? " reference checked, " : " references checked, ") +
          (shippable() ? "no finding blocks this use" : "blocked");
  return text;
}

TerrainAssetValidationReport
validateTerrainAssets(const TerrainAssetValidationRequest &request) {
  if (request.registry == nullptr)
    throw std::invalid_argument(
        "terrain asset validation requires an asset registry: there is nothing "
        "to inspect without one.");
  if (request.paletteId.empty() && request.materialSetId.empty())
    throw std::invalid_argument(
        "terrain asset validation needs a palette or a material set: pass a "
        "palette id, a material set id, or both.");

  Findings findings(*request.registry, request.requireFullMaterialMaps);
  if (!request.paletteId.empty())
    inspectPalette(findings, request.paletteId);
  if (!request.materialSetId.empty())
    inspectMaterialSet(findings, request.materialSetId);

  TerrainAssetValidationReport report;
  report.findings = findings.take();
  report.checked = findings.checked();
  report.use = request.use;
  report.requireFullMaterialMaps = request.requireFullMaterialMaps;
  // Sorted by kind, then role, then asset, so a diff of two runs against the
  // same project is readable. Stable, so two findings that share all three keys
  // keep the order they were collected in.
  std::stable_sort(report.findings.begin(), report.findings.end(),
                   [](const TerrainAssetFinding &left,
                      const TerrainAssetFinding &right) {
                     const auto leftKind =
                         static_cast<int>(left.kind);
                     const auto rightKind =
                         static_cast<int>(right.kind);
                     if (leftKind != rightKind)
                       return leftKind < rightKind;
                     if (left.role != right.role)
                       return left.role < right.role;
                     return left.assetId < right.assetId;
                   });
  return report;
}

} // namespace demi::assets
