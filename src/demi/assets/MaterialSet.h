#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi {
struct AssetRegistry;
namespace assets {
struct DataDocument;
}
} // namespace demi

namespace demi::assets {

// The closed set of terrain surface roles a material set may fill. The
// vocabulary is fixed so a typo is a load error instead of a role that no mask
// ever assigns.
//
// The roles describe the SURFACE a generated terrain exposes, not the props a
// palette scatters on it: that is why vegetation ground layers and sediment
// appear here while trees and debris do not.
enum class TerrainMaterialRole {
  Rock,      // steep or high exposed ground
  Cliff,     // near-vertical faces, which need a triplanar material
  Sediment,  // low-slope deposition areas
  Ground,    // vegetated soil cover
  Grass,     // dense grass layer
  Sand,      // dry beach and shallow shore
  Snow,      // where the biome permits snow
  WetGround, // shoreline inside the wet band
  Underwater,// submerged bed
};

// A reusable map from surface roles to imported material assets. It is a
// DataAsset document with settings.content_type "terrain_material_set", so cook,
// packaging, dependency closure, demi validate, Lua Data.load and hot reload all
// apply without a second implementation.
//
// Roles are stored as an object keyed by role name because DataValue::Object is
// key-sorted: a duplicated role is then structurally impossible and iteration
// order is deterministic.
struct TerrainMaterialSet {
  static constexpr int formatVersion = 1;
  // The asset:// id the set was loaded from.
  std::string id;
  std::string name;
  std::string description;
  std::map<std::string, std::string, std::less<>> roles;
  // Roles a consumer must find to treat the set as usable. Sorted and
  // deduplicated so the order never depends on the authored array.
  std::vector<std::string> requiredRoles;

  // Every asset:// this set depends on, sorted and deduped.
  [[nodiscard]] std::vector<std::string> assetDependencies() const;
};

[[nodiscard]] std::string_view terrainMaterialRoleName(TerrainMaterialRole role);
[[nodiscard]] std::optional<TerrainMaterialRole>
terrainMaterialRoleFromName(std::string_view name);

// Loads and fully validates a terrain_material_set DataAsset. Throws
// std::invalid_argument with an actionable message on any problem, including a
// missing id or a manifest that is not a terrain_material_set DataAsset.
// Manifest dependency declaration is left to validateDataAssets(), which owns
// it.
[[nodiscard]] std::optional<TerrainMaterialSet>
loadTerrainMaterialSet(const AssetRegistry &registry, std::string_view id);

// Validates an already-parsed set document, with the same rules as the loader.
// The registry is required because a role that does not resolve is a load
// error, not a deferred render warning.
[[nodiscard]] std::optional<TerrainMaterialSet>
parseTerrainMaterialSet(const DataDocument &document,
                        const AssetRegistry &registry, std::string_view id);

} // namespace demi::assets