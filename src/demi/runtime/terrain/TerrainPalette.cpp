#include "demi/runtime/terrain/TerrainPalette.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/DataDocument.h"
#include "demi/assets/DataValueRead.h"
#include "demi/assets/MaterialSet.h"
#include "demi/diagnostics/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>

namespace demi::runtime {
namespace {

using assets::DataValue;

constexpr std::string_view ContentType = "terrain_palette";
// The document kind every palette rejection is prefixed with.
constexpr std::string_view Kind = "terrain palette";
constexpr std::string_view AssetPrefix = "asset://";
constexpr std::string_view PrefabPrefix = "prefab://";

struct PolicyName {
  TerrainCollisionPolicy policy;
  std::string_view name;
};

constexpr PolicyName CollisionPolicies[]{
    {TerrainCollisionPolicy::None, "none"},
    {TerrainCollisionPolicy::Static, "static"},
    {TerrainCollisionPolicy::Trigger, "trigger"},
};

[[noreturn]] void fail(std::string message) {
  throw std::invalid_argument(std::move(message));
}

void require(const bool condition, std::string message) {
  if (!condition)
    fail(std::move(message));
}

// Messages name the exact document location so an authoring mistake is a
// one-glance fix instead of a search through the palette.
std::string at(const std::string_view where) {
  return "terrain palette " + std::string(where) + ": ";
}

std::string join(const std::vector<std::string_view> &names) {
  std::string joined;
  for (const std::string_view name : names) {
    if (!joined.empty())
      joined += ", ";
    joined += name;
  }
  return joined;
}

void checkFields(const DataValue &value, const std::string_view where,
                 const std::vector<std::string_view> &allowed) {
  const auto *object = value.object();
  require(object != nullptr, at(where) + "must be an object.");
  for (const auto &[key, unused] : *object) {
    (void)unused;
    require(std::ranges::find(allowed.begin(), allowed.end(), key) !=
                allowed.end(),
            at(where) + "has unknown field \"" + key + "\". Known fields: " +
                join(allowed) + ".");
  }
}

// data_read::number accepts any finite JSON number and rejects a non-number
// with a different wording; a palette placement value also has to survive the
// float conversion, which is why this read is not delegated.
float number(const DataValue &value, const std::string_view where) {
  const double parsed =
      value.isInteger()
          ? static_cast<double>(std::get<std::int64_t>(value.value))
          : value.isNumber() ? std::get<double>(value.value) : 0.0;
  const float result = static_cast<float>(parsed);
  require(value.isNumber() && std::isfinite(result),
          at(where) + "must be a finite number.");
  return result;
}

// data_read::integer widens an authored integer straight to int, so the guard
// against a lod that does not survive that narrowing is kept here.
int integer(const DataValue &value, const std::string_view where) {
  require(value.isInteger(), at(where) + "must be an integer.");
  const auto parsed = std::get<std::int64_t>(value.value);
  require(parsed >= std::numeric_limits<int>::min() &&
              parsed <= std::numeric_limits<int>::max(),
          at(where) + "is out of range.");
  return static_cast<int>(parsed);
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

void readEntry(const DataValue &value, const std::string_view roleName,
               const AssetRegistry &registry, TerrainPaletteEntry &entry) {
  const std::string where = "placements/" + std::string(roleName);
  checkFields(value, where,
              {"model", "prefab", "weight", "scale", "spacing", "collision",
               "lod", "biomes"});
  const auto *model = value.find("model");
  const auto *prefab = value.find("prefab");
  require((model != nullptr) != (prefab != nullptr),
          at(where) + "requires exactly one of model or prefab.");
  if (model) {
    entry.model = assets::data_read::text(*model, Kind, where + "/model");
    const auto *target = findAsset(registry, entry.model);
    require(entry.model.starts_with(AssetPrefix) && target &&
                target->type == "Model3D",
            at(where + "/model") + "must resolve to a Model3D asset.");
  } else {
    entry.prefab = assets::data_read::text(*prefab, Kind, where + "/prefab");
    require(entry.prefab.starts_with(PrefabPrefix) &&
                entry.prefab.size() > PrefabPrefix.size(),
            at(where + "/prefab") + "must be a prefab:// reference.");
  }
  if (const DataValue *weight = value.find("weight"); weight != nullptr) {
    const std::string weightWhere = where + "/weight";
    entry.weight = number(*weight, weightWhere);
    require(entry.weight >= 0.F, at(weightWhere) +
                                     "must be at least 0, but is " +
                                     std::to_string(entry.weight) + ".");
  }
  if (const DataValue *scale = value.find("scale"); scale != nullptr) {
    const std::string scaleWhere = where + "/scale";
    const auto *range = scale->array();
    require(range != nullptr && range->size() == 2,
            at(scaleWhere) + "must be [scale_min, scale_max].");
    entry.scaleMin = number((*range)[0], scaleWhere + "[0]");
    entry.scaleMax = number((*range)[1], scaleWhere + "[1]");
    require(entry.scaleMin > 0.F, at(scaleWhere) +
                                      "scale_min must be greater than 0, but "
                                      "is " +
                                      std::to_string(entry.scaleMin) + ".");
    require(entry.scaleMin <= entry.scaleMax,
            at(scaleWhere) + "scale_min must not exceed scale_max, but is " +
                std::to_string(entry.scaleMin) + " > " +
                std::to_string(entry.scaleMax) + ".");
  }
  if (const DataValue *spacing = value.find("spacing"); spacing != nullptr) {
    const std::string spacingWhere = where + "/spacing";
    entry.spacing = number(*spacing, spacingWhere);
    require(entry.spacing >= 0.F,
            at(spacingWhere) + "must be at least 0 world units, but is " +
                std::to_string(entry.spacing) + ".");
  }
  if (const DataValue *policy = value.find("collision"); policy != nullptr) {
    const std::string policyWhere = where + "/collision";
    const std::string policyName =
        assets::data_read::text(*policy, Kind, policyWhere);
    std::optional<TerrainCollisionPolicy> parsed;
    for (const PolicyName &candidate : CollisionPolicies)
      if (candidate.name == policyName) {
        parsed = candidate.policy;
        break;
      }
    require(parsed.has_value(), at(policyWhere) +
                                    "has unknown collision policy \"" +
                                    policyName +
                                    "\". Use none, static, or trigger.");
    entry.collision = *parsed;
  }
  if (const DataValue *lod = value.find("lod"); lod != nullptr) {
    const std::string lodWhere = where + "/lod";
    entry.lod = integer(*lod, lodWhere);
    require(entry.lod >= 0, at(lodWhere) + "must be at least 0, but is " +
                                std::to_string(entry.lod) + ".");
  }
  if (const DataValue *biomes = value.find("biomes"); biomes != nullptr) {
    const std::string biomesWhere = where + "/biomes";
    const DataValue::Array *names = biomes->array();
    require(names != nullptr,
            at(biomesWhere) + "must be an array of biome names.");
    for (const auto &name : *names) {
      require(name.isString(),
              at(biomesWhere) + "must contain only strings.");
      auto text = std::get<std::string>(name.value);
      require(!text.empty(), at(biomesWhere) + "must not contain an empty name.");
      // Sorted and deduplicated, so a role behaves identically however the
      // author ordered or repeated the list.
      if (std::find(entry.biomes.begin(), entry.biomes.end(), text) ==
          entry.biomes.end())
        entry.biomes.push_back(std::move(text));
    }
    std::sort(entry.biomes.begin(), entry.biomes.end());
  }
}

} // namespace

std::vector<std::string> TerrainPalette::assetDependencies() const {
  std::set<std::string, std::less<>> unique;
  if (!materialSet.empty())
    unique.insert(materialSet);
  for (const auto &[role, entry] : placements) {
    (void)role;
    if (!entry.model.empty())
      unique.insert(entry.model);
    if (!entry.prefab.empty())
      unique.insert(entry.prefab);
  }
  return {unique.begin(), unique.end()};
}

bool validTerrainPlacementRuleId(std::string_view id) {
  return !id.empty() && std::ranges::all_of(id, [](unsigned char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_' ||
           character == '-';
  });
}

std::uint64_t terrainPlacementRuleHash(std::string_view id) {
  // FNV-1a over UTF-8 bytes; stable across platforms and library versions.
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char byte : id) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string_view terrainCollisionPolicyName(
    const TerrainCollisionPolicy policy) {
  for (const PolicyName &entry : CollisionPolicies)
    if (entry.policy == policy)
      return entry.name;
  return "static";
}

std::optional<TerrainPalette>
loadTerrainPalette(const AssetRegistry &registry, const std::string_view id) {
  const std::string assetId(id);
  const AssetManifest *manifest = findAsset(registry, assetId);
  require(manifest != nullptr,
          "terrain palette asset was not found: " + assetId + ".");
  require(manifest->type == "DataAsset",
          "terrain palette asset must be a DataAsset: " + assetId + " is " +
              manifest->type + ".");
  Diagnostics diagnostics;
  const auto loaded = assets::loadDataAsset(*manifest, {}, &diagnostics);
  require(loaded.has_value(), "terrain palette " + assetId +
                                  " could not be read: " +
                                  summarize(diagnostics) + ".");
  require(loaded->metadata.contentType == ContentType,
          "terrain palette " + assetId +
              " must declare settings.content_type \"" +
              std::string(ContentType) + "\", but declares \"" +
              loaded->metadata.contentType + "\".");
  return parseTerrainPalette(*loaded->document, registry, id);
}

TerrainPalette parseTerrainPalette(const assets::DataDocument &document,
                                   const AssetRegistry &registry,
                                   const std::string_view id) {
  TerrainPalette palette;
  palette.id = std::string(id);

  const DataValue &root = document.root();
  const auto *version = root.find("format_version");
  require(version && version->isInteger() &&
              std::get<std::int64_t>(version->value) ==
                  TerrainPalette::CurrentFormatVersion,
          at("format_version") +
              "must be 2. Migrate surface roles to a material_set and object "
              "roles to named placements.");
  checkFields(root, "document",
              {"format_version", "name", "material_set", "placements"});
  const DataValue *name = assets::data_read::field(root, "name");
  require(name != nullptr, at("name") + "is required.");
  require(name->isString() && !std::get<std::string>(name->value).empty(),
          at("name") + "must be a non-empty string.");
  palette.name = std::get<std::string>(name->value);

  if (const auto *set = root.find("material_set")) {
    palette.materialSet = assets::data_read::text(*set, Kind, "material_set");
    require(palette.materialSet.starts_with(AssetPrefix),
            at("material_set") + "must be an asset:// reference.");
    require(assets::loadTerrainMaterialSet(registry, palette.materialSet)
                .has_value(),
            at("material_set") + "must resolve to a terrain_material_set.");
  }
  if (const auto *placements = root.find("placements")) {
    const auto *entries = placements->object();
    require(entries != nullptr,
            at("placements") + "must be an object keyed by stable rule ID.");
    for (const auto &[ruleId, value] : *entries) {
      require(validTerrainPlacementRuleId(ruleId),
              at("placements/" + ruleId) +
                  "rule IDs use letters, digits, underscores or hyphens.");
      TerrainPaletteEntry entry;
      readEntry(value, ruleId, registry, entry);
      palette.placements.emplace(ruleId, std::move(entry));
    }
  }
  require(!palette.materialSet.empty() || !palette.placements.empty(),
          at("document") +
              "must supply a material_set or at least one placement.");
  return palette;
}

} // namespace demi::runtime
