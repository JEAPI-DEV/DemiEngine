#pragma once

#include <cstddef>
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
}

namespace demi::runtime {

// What a scattered instance contributes to the physics world. None leaves the
// decoration pass entirely, Trigger is enterable but immovable, Static is a
// full blocker.
enum class TerrainCollisionPolicy { None, Static, Trigger };

// The closed set of semantic roles a palette may fill. The vocabulary is fixed
// so a typo is a load error instead of a role that silently never scatters.
enum class TerrainPaletteRole {
  ExposedRock,
  Soil,
  Sand,
  Snow,
  WetGround,
  Tree,
  Bush,
  Grass,
  Reed,
  CliffPiece,
  Debris,
};

struct TerrainPaletteEntry {
  TerrainPaletteRole role = TerrainPaletteRole::Soil;
  std::string asset;
  std::string prefab;
  // Relative selection weight against the other roles. 0 keeps the role
  // available but never selected.
  float weight = 1;
  float scaleMin = 1;
  float scaleMax = 1;
  // World units between instances. 0 leaves placement unconstrained, which is
  // what continuous ground cover needs and clumped props must not use.
  float spacing = 1;
  TerrainCollisionPolicy collision = TerrainCollisionPolicy::Static;
  int lod = 0;
  // Biomes this role may appear in, by biome name. Empty means every biome.
  // Ground cover wants this narrow (grass on meadow, rock on rock) while a
  // global prop such as debris may deliberately span everything. Sorted and
  // deduplicated so placement never depends on the authored array order.
  std::vector<std::string> biomes;
};

// A reusable map from semantic roles to mesh assets or geometry prefabs. A
// material may be named when a prefab supplies geometry. It is a DataAsset
// document, so cook,
// packaging, dependency closure, demi validate, Lua Data.load and hot reload
// all apply without a second implementation.
//
// The document stores roles as an object keyed by role name rather than as an
// array of entries, because DataValue::Object is key-sorted: duplicate roles are
// then structurally impossible and iteration order is deterministic.
struct TerrainPalette {
  int formatVersion = 1;
  // The asset:// id the palette was loaded from.
  std::string id;
  std::string name;
  std::map<std::string, TerrainPaletteEntry, std::less<>> roles;
  // Roles a consumer must find to treat the palette as usable. Sorted and
  // deduplicated so the order never depends on the authored array.
  std::vector<std::string> requiredRoles;

  // Every asset:// and prefab:// this palette depends on, sorted and deduped.
  [[nodiscard]] std::vector<std::string> assetDependencies() const;
};

[[nodiscard]] std::string_view terrainPaletteRoleName(TerrainPaletteRole role);
[[nodiscard]] std::optional<TerrainPaletteRole>
terrainPaletteRoleFromName(std::string_view name);
[[nodiscard]] std::string_view
terrainCollisionPolicyName(TerrainCollisionPolicy policy);

// Loads and fully validates a terrain_palette DataAsset. Throws
// std::invalid_argument with an actionable message on any problem, including a
// missing id or a manifest that is not a terrain_palette DataAsset. Manifest
// dependency declaration is left to validateDataAssets(), which owns it.
[[nodiscard]] std::optional<TerrainPalette>
loadTerrainPalette(const AssetRegistry &registry, std::string_view id);

// Validates an already-parsed palette document. A consumer that already holds
// the document, such as Lua Data.load or a hot-reloaded snapshot, must not have
// to re-read the file to reach the same rules.
[[nodiscard]] TerrainPalette
parseTerrainPalette(const assets::DataDocument &document,
                    const AssetRegistry &registry, std::string_view id);

} // namespace demi::runtime
