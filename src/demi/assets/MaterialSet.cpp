#include "demi/assets/MaterialSet.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/DataDocument.h"
#include "demi/assets/DataValueRead.h"
#include "demi/assets/MaterialAsset.h"
#include "demi/diagnostics/Diagnostic.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace demi::assets {
namespace {

constexpr std::string_view Kind = "terrain material set";
constexpr std::string_view ContentType = "terrain_material_set";
constexpr std::string_view AssetPrefix = "asset://";

struct RoleName {
  TerrainMaterialRole role;
  std::string_view name;
};

constexpr RoleName MaterialRoles[]{
    {TerrainMaterialRole::Rock, "rock"},
    {TerrainMaterialRole::Cliff, "cliff"},
    {TerrainMaterialRole::Sediment, "sediment"},
    {TerrainMaterialRole::Ground, "ground"},
    {TerrainMaterialRole::Grass, "grass"},
    {TerrainMaterialRole::Sand, "sand"},
    {TerrainMaterialRole::Snow, "snow"},
    {TerrainMaterialRole::WetGround, "wet_ground"},
    {TerrainMaterialRole::Underwater, "underwater"},
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

// Derived from the vocabulary table rather than repeated, so a new role can
// never be accepted by the loader but missing from its own error message.
std::string knownRoles() {
  std::vector<std::string_view> names;
  for (const RoleName &entry : MaterialRoles)
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

// A role must bind a material, not a texture or an audio clip. Both material
// carriers count: a render Material asset and a terrain_material DataAsset are
// the same thing to a surface assignment, and a set must not depend on which of
// the two an author happened to use.
std::string readRoleMaterial(const DataValue &value,
                             const std::string_view where,
                             const AssetRegistry &registry) {
  const std::string reference = data_read::text(value, Kind, where);
  data_read::require(!reference.empty(), at(where) + "must not be empty.");
  data_read::require(reference.starts_with(AssetPrefix),
                     at(where) + "must be an asset:// reference, but is \"" +
                         reference + "\".");
  const AssetManifest *manifest = findAsset(registry, reference);
  data_read::require(manifest != nullptr,
                     at(where) + "does not resolve: " + reference +
                         ". Add the asset manifest or fix the reference.");
  if (manifest->type == "Material")
    return reference;
  const std::optional<DataAssetMetadata> metadata =
      manifest->type == "DataAsset" ? dataAssetMetadata(*manifest)
                                    : std::nullopt;
  data_read::require(
      metadata.has_value() && metadata->contentType == "terrain_material",
      at(where) + "is not a material asset: " + reference + " is " +
          manifest->type +
          ". Use a Material asset or a DataAsset declaring "
          "settings.content_type \"terrain_material\".");
  // A bound material has to load under the material rules now, not at render
  // time: demi validate only checks a document against its schema, so a broken
  // material document would otherwise reach a surface unchallenged.
  try {
    (void)loadTerrainMaterialAsset(registry, reference);
  } catch (const std::invalid_argument &failure) {
    data_read::reject(at(where) +
                      "references a material that does not load: " +
                      std::string(failure.what()));
  }
  return reference;
}

} // namespace

std::vector<std::string> TerrainMaterialSet::assetDependencies() const {
  std::set<std::string, std::less<>> unique;
  for (const auto &[role, material] : roles) {
    (void)role;
    if (!material.empty())
      unique.insert(material);
  }
  return {unique.begin(), unique.end()};
}

std::string_view terrainMaterialRoleName(const TerrainMaterialRole role) {
  for (const RoleName &entry : MaterialRoles)
    if (entry.role == role)
      return entry.name;
  return "rock";
}

std::optional<TerrainMaterialRole>
terrainMaterialRoleFromName(const std::string_view name) {
  for (const RoleName &entry : MaterialRoles)
    if (entry.name == name)
      return entry.role;
  return std::nullopt;
}

std::optional<TerrainMaterialSet>
loadTerrainMaterialSet(const AssetRegistry &registry,
                        const std::string_view id) {
  const auto loaded =
      material_detail::loadDataAssetOfType(registry, id, Kind, ContentType);
  return parseTerrainMaterialSet(*loaded->document, registry, id);
}

std::optional<TerrainMaterialSet>
parseTerrainMaterialSet(const DataDocument &document,
                        const AssetRegistry &registry,
                        const std::string_view id) {
  TerrainMaterialSet set;
  set.id = std::string(id);

  const DataValue &root = document.root();
  checkFields(root,
              {"format_version", "name", "description", "roles",
               "required_roles"});
  // The header reports a wrong version and a wrong type the same way: the
  // document declares this format, so there is nothing else it may declare.
  const DataValue *version = field(root, "format_version");
  data_read::require(version->isInteger() &&
                         std::get<std::int64_t>(version->value) ==
                             TerrainMaterialSet::formatVersion,
                     at("format_version") + "must be 1.");
  set.name = data_read::text(*field(root, "name"), Kind, "name");
  data_read::require(!set.name.empty(),
                     at("name") + "must be a non-empty string.");
  if (const DataValue *description = root.find("description");
      description != nullptr)
    set.description = data_read::text(*description, Kind, "description");

  const DataValue *roles = field(root, "roles");
  const auto *roleMap = roles->object();
  data_read::require(roleMap != nullptr && !roleMap->empty(),
                     at("roles") +
                         "must be a non-empty object keyed by role name. Known "
                         "roles: " +
                         knownRoles() + ".");
  for (const auto &[roleName, value] : *roleMap) {
    // An unknown role is an error, not a silently unused binding: the surface it
    // was meant for would keep whatever material the previous set left there.
    data_read::require(terrainMaterialRoleFromName(roleName).has_value(),
                       at("roles/" + roleName) +
                           "is not a known role. Known roles: " + knownRoles() +
                           ".");
    set.roles.emplace(roleName, readRoleMaterial(value, "roles/" + roleName,
                                                 registry));
  }

  if (const DataValue *required = root.find("required_roles");
      required != nullptr) {
    const auto *list = required->array();
    data_read::require(list != nullptr,
                       at("required_roles") +
                           "must be an array of role names.");
    std::set<std::string, std::less<>> unique;
    for (std::size_t index = 0; index < list->size(); ++index) {
      const std::string where = "required_roles[" + std::to_string(index) + "]";
      const std::string roleName = data_read::text((*list)[index], Kind, where);
      data_read::require(terrainMaterialRoleFromName(roleName).has_value(),
                         at(where) + "is not a known role. Known roles: " +
                             knownRoles() + ".");
      data_read::require(set.roles.contains(roleName),
                         at(where) + "requires role \"" + roleName +
                             "\", which the set does not define.");
      unique.insert(roleName);
    }
    set.requiredRoles.assign(unique.begin(), unique.end());
  }
  return set;
}

} // namespace demi::assets