#include "demi/assets/MaterialAsset.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/DataDocument.h"
#include "demi/assets/DataValueRead.h"
#include "demi/diagnostics/Diagnostic.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <utility>

namespace demi::assets {
namespace {

constexpr std::string_view Kind = "terrain material";
constexpr std::string_view ContentType = "terrain_material";
constexpr std::string_view AssetPrefix = "asset://";

struct SlotName {
  TerrainMaterialMapSlot slot;
  std::string_view name;
};

// Ordered by slot so a serialized document and an error listing both read in the
// same sequence.
constexpr SlotName MapSlots[]{
    {TerrainMaterialMapSlot::BaseColor, "base_color"},
    {TerrainMaterialMapSlot::Normal, "normal"},
    {TerrainMaterialMapSlot::Roughness, "roughness"},
    {TerrainMaterialMapSlot::Metallic, "metallic"},
    {TerrainMaterialMapSlot::AmbientOcclusion, "ambient_occlusion"},
    {TerrainMaterialMapSlot::Height, "height"},
    {TerrainMaterialMapSlot::Detail, "detail"},
    {TerrainMaterialMapSlot::Emissive, "emissive"},
};

std::string join(const std::vector<std::string_view> &names) {
  std::string joined;
  for (const std::string_view name : names) {
    if (!joined.empty())
      joined += ", ";
    joined += name;
  }
  return joined;
}

// Derived from the vocabulary table rather than repeated, so a new slot can
// never be accepted by the loader but missing from its own error message.
std::string knownSlots() {
  std::vector<std::string_view> names;
  for (const SlotName &entry : MapSlots)
    names.push_back(entry.name);
  return join(names);
}

// Every rejection names the document location, so an authoring mistake is a
// one-glance fix instead of a search through the document.
std::string at(const std::string_view where) {
  return data_read::at(Kind, where);
}

void checkFields(const DataValue &value,
                 const std::vector<std::string_view> &allowed) {
  data_read::requireObject(value, Kind, "document");
  for (const auto &[key, unused] : *value.object()) {
    (void)unused;
    data_read::require(
        std::ranges::find(allowed.begin(), allowed.end(), key) != allowed.end(),
        at("document") + "has unknown field \"" + key +
            "\". Known fields: " + join(allowed) + ".");
  }
}

const DataValue *field(const DataValue &object, const std::string_view key) {
  const DataValue *value = data_read::field(object, key);
  data_read::require(value != nullptr, at(key) + "is required.");
  return value;
}

// data_read owns reading both JSON number representations; the material keeps
// its own rejection, which names the fault rather than the JSON type.
float number(const DataValue &value, const std::string_view where) {
  data_read::require(value.isNumber(), at(where) + "must be a finite number.");
  const float result = data_read::number(value, Kind, where);
  data_read::require(std::isfinite(result),
                     at(where) + "must be a finite number.");
  return result;
}

float ranged(const DataValue &value, const std::string_view where,
             const float minimum, const float maximum) {
  const float parsed = number(value, where);
  data_read::require(parsed >= minimum && parsed <= maximum,
                     at(where) + "must be between " +
                         data_read::readable(minimum) + " and " +
                         data_read::readable(maximum) + ", but is " +
                         data_read::readable(parsed) + ".");
  return parsed;
}

// Not data_read::color: the author typed "base_color", so the rejection names
// the field and its bound instead of an array index and a quad shape.
runtime::Color readColor(const DataValue &value) {
  const auto *channels = value.array();
  data_read::require(channels != nullptr && channels->size() == 4,
                     at("base_color") +
                         "must be [r, g, b, a] with four channels.");
  runtime::Color color;
  float *values[] = {&color.r, &color.g, &color.b, &color.a};
  for (std::size_t index = 0; index < 4; ++index) {
    const std::string where = "base_color[" + std::to_string(index) + "]";
    const float channel = number((*channels)[index], where);
    // Channels are linear, so a value outside 0 to 1 is a broken colour space
    // conversion rather than a look the shading pipeline can reproduce.
    data_read::require(channel >= 0.F && channel <= 1.F,
                       at(where) + "must be between 0 and 1, but is " +
                           data_read::readable(channel) + ".");
    *values[index] = channel;
  }
  return color;
}

void readMaps(const DataValue &value, const AssetRegistry &registry,
              std::map<TerrainMaterialMapSlot, std::string, std::less<>> &maps) {
  const auto *entries = value.array();
  data_read::require(entries != nullptr,
                     at("maps") +
                         "must be an array of single-slot objects, for example "
                         "[{\"normal\": \"asset://terrain/normal\"}].");
  for (std::size_t index = 0; index < entries->size(); ++index) {
    const std::string where = "maps[" + std::to_string(index) + "]";
    const auto *slots = (*entries)[index].object();
    data_read::require(slots != nullptr && slots->size() == 1,
                       at(where) +
                           "must name exactly one map slot. Known slots: " +
                           knownSlots() + ".");
    const auto &entry = *slots->begin();
    const auto slot = terrainMaterialMapSlotFromName(entry.first);
    // An unknown slot is an error: silently dropping it would ship a material
    // that looks finished and shades wrong.
    data_read::require(slot.has_value(),
                       at(where) + "has unknown map slot \"" + entry.first +
                           "\". Known slots: " + knownSlots() + ".");
    const std::string slotWhere = where + "/" + entry.first;
    const std::string reference =
        data_read::text(entry.second, Kind, slotWhere);
    data_read::require(!reference.empty(),
                       at(slotWhere) + "must not be empty.");
    data_read::require(reference.starts_with(AssetPrefix),
                       at(slotWhere) +
                           "must be an asset:// reference, but is \"" +
                           reference + "\".");
    data_read::require(findAsset(registry, reference) != nullptr,
                       at(slotWhere) + "does not resolve: " + reference +
                           ". Add the asset manifest or fix the reference.");
    // Two entries for one slot are a mistake, not a last-one-wins override: the
    // author cannot tell which map survived.
    data_read::require(!maps.contains(*slot),
                       at(slotWhere) + "repeats the " + entry.first +
                           " slot, which is already assigned by an earlier "
                           "entry.");
    maps.emplace(*slot, reference);
  }
}

bool defaultColor(const runtime::Color &color) {
  return color.r == 0.8F && color.g == 0.8F && color.b == 0.8F &&
         color.a == 1.F;
}

} // namespace

namespace material_detail {

// Declared in MaterialAsset.h and shared with MaterialSet.cpp. The message
// conventions themselves now live in data_read, so these are the exported names
// reduced to one line each rather than a second implementation of the rules.
void fail(std::string message) { data_read::reject(std::move(message)); }

void require(const bool condition, std::string message) {
  data_read::require(condition, std::move(message));
}

std::string at(const std::string_view kind, const std::string_view where) {
  return data_read::at(kind, where);
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

std::optional<LoadedDataAsset>
loadDataAssetOfType(const AssetRegistry &registry, const std::string_view id,
                    const std::string_view kind,
                    const std::string_view contentType) {
  const std::string assetId(id);
  const AssetManifest *manifest = findAsset(registry, assetId);
  require(manifest != nullptr,
          std::string(kind) + " asset was not found: " + assetId + ".");
  require(manifest->type == "DataAsset",
          std::string(kind) + " asset must be a DataAsset: " + assetId +
              " is " + manifest->type + ".");
  Diagnostics diagnostics;
  const auto loaded = loadDataAsset(*manifest, {}, &diagnostics);
  require(loaded.has_value(), std::string(kind) + " " + assetId +
                                  " could not be read: " +
                                  summarize(diagnostics) + ".");
  require(loaded->metadata.contentType == contentType,
          std::string(kind) + " " + assetId +
              " must declare settings.content_type \"" +
              std::string(contentType) + "\", but declares \"" +
              loaded->metadata.contentType + "\".");
  return loaded;
}

std::string readString(const DataValue &value, const std::string_view kind,
                       const std::string_view where) {
  return data_read::text(value, kind, where);
}

} // namespace material_detail

std::string_view terrainMaterialMapSlotName(const TerrainMaterialMapSlot slot) {
  for (const SlotName &entry : MapSlots)
    if (entry.slot == slot)
      return entry.name;
  return "base_color";
}

std::optional<TerrainMaterialMapSlot>
terrainMaterialMapSlotFromName(const std::string_view name) {
  for (const SlotName &entry : MapSlots)
    if (entry.name == name)
      return entry.slot;
  return std::nullopt;
}

std::vector<std::string> TerrainMaterialAsset::assetDependencies() const {
  std::set<std::string, std::less<>> unique;
  for (const auto &[slot, reference] : maps) {
    (void)slot;
    if (!reference.empty())
      unique.insert(reference);
  }
  return {unique.begin(), unique.end()};
}

nlohmann::json TerrainMaterialAsset::toJson() const {
  nlohmann::json document;
  document["format_version"] = formatVersion;
  document["name"] = name;
  // Defaults stay omitted, so an authored document only states what it means to
  // change and the parser fills the rest from the same canonical values.
  if (!defaultColor(baseColor))
    document["base_color"] = nlohmann::json::array(
        {baseColor.r, baseColor.g, baseColor.b, baseColor.a});
  if (roughness != 0.8F)
    document["roughness"] = roughness;
  if (metallic != 0.F)
    document["metallic"] = metallic;
  if (normalStrength != 1.F)
    document["normal_strength"] = normalStrength;
  if (tiling != 1.F)
    document["tiling"] = tiling;
  if (detailStrength != 0.F)
    document["detail_strength"] = detailStrength;
  if (triplanar)
    document["triplanar"] = true;
  if (!maps.empty()) {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto &[slot, reference] : maps) {
      nlohmann::json entry;
      entry[std::string(terrainMaterialMapSlotName(slot))] = reference;
      entries.push_back(std::move(entry));
    }
    document["maps"] = std::move(entries);
  }
  return document;
}

std::optional<TerrainMaterialAsset>
loadTerrainMaterialAsset(const AssetRegistry &registry,
                           const std::string_view id) {
  const auto loaded =
      material_detail::loadDataAssetOfType(registry, id, Kind, ContentType);
  return parseTerrainMaterialAsset(*loaded->document, registry, id);
}

std::optional<TerrainMaterialAsset>
parseTerrainMaterialAsset(const DataDocument &document,
                          const AssetRegistry &registry,
                          const std::string_view id) {
  TerrainMaterialAsset material;
  material.id = std::string(id);

  const DataValue &root = document.root();
  checkFields(root,
              {"format_version", "name", "base_color", "roughness", "metallic",
               "normal_strength", "tiling", "detail_strength", "triplanar",
               "maps"});
  // The header reports a wrong version and a wrong type the same way: the
  // document declares this format, so there is nothing else it may declare.
  const DataValue *version = field(root, "format_version");
  data_read::require(version->isInteger() &&
                         std::get<std::int64_t>(version->value) ==
                             TerrainMaterialAsset::formatVersion,
                     at("format_version") + "must be 1.");
  material.name = data_read::text(*field(root, "name"), Kind, "name");
  data_read::require(!material.name.empty(),
                     at("name") + "must be a non-empty string.");

  if (const DataValue *base = root.find("base_color"); base != nullptr)
    material.baseColor = readColor(*base);
  if (const DataValue *value = root.find("roughness"); value != nullptr)
    material.roughness = ranged(*value, "roughness", 0.F, 1.F);
  if (const DataValue *value = root.find("metallic"); value != nullptr)
    material.metallic = ranged(*value, "metallic", 0.F, 1.F);
  if (const DataValue *value = root.find("normal_strength");
      value != nullptr) {
    // 0 flattens the authored normal, 1 is the authored normal, and 2 to 4
    // amplifies a high-frequency detail normal that would otherwise read as too
    // subtle at play distance. Beyond 4 the amplified vector no longer
    // represents a plausible surface, and shading turns into banding and
    // specular noise instead of relief.
    material.normalStrength = ranged(*value, "normal_strength", 0.F, 4.F);
  }
  if (const DataValue *value = root.find("tiling"); value != nullptr) {
    // Repeats across one authored surface unit: 0 collapses the projection and a
    // negative value mirrors it, so neither can shade.
    material.tiling = number(*value, "tiling");
    data_read::require(material.tiling > 0.F,
                       at("tiling") + "must be greater than 0, but is " +
                           std::to_string(material.tiling) + ".");
  }
  if (const DataValue *value = root.find("detail_strength");
      value != nullptr) {
    material.detailStrength = number(*value, "detail_strength");
    data_read::require(material.detailStrength >= 0.F,
                       at("detail_strength") + "must be at least 0, but is " +
                           data_read::readable(material.detailStrength) + ".");
  }
  if (const DataValue *value = root.find("triplanar"); value != nullptr) {
    data_read::require(value->isBoolean(),
                       at("triplanar") + "must be a boolean.");
    material.triplanar = std::get<bool>(value->value);
  }
  if (const DataValue *maps = root.find("maps"); maps != nullptr)
    readMaps(*maps, registry, material.maps);

  // A detail strength with nothing to sample is a setting that silently does
  // nothing at runtime.
  data_read::require(material.detailStrength == 0.F ||
                         material.maps.contains(TerrainMaterialMapSlot::Detail),
                     at("detail_strength") + "is " +
                         std::to_string(material.detailStrength) +
                         " but no detail map is assigned. Add a detail map "
                         "or set detail_strength to 0.");
  return material;
}

} // namespace demi::assets