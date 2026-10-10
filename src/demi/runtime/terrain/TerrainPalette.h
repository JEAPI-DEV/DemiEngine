#pragma once

#include <cstddef>
#include <cstdint>
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

struct TerrainPaletteEntry {
  std::string model;
  std::string prefab;
  // Relative selection weight against the other rules. 0 keeps the role
  // available but never selected.
  float weight = 1;
  float scaleMin = 1;
  float scaleMax = 1;
  // World units between instances. 0 leaves placement unconstrained, which is
  // what continuous ground cover needs and clumped props must not use.
  float spacing = 1;
  TerrainCollisionPolicy collision = TerrainCollisionPolicy::Static;
  int lod = 0;
  // Biomes this rule may appear in, by biome name. Empty means every biome.
  // Ground cover wants this narrow (grass on meadow, rock on rock) while a
  // global prop such as debris may deliberately span everything. Sorted and
  // deduplicated so placement never depends on the authored array order.
  std::vector<std::string> biomes;
};

// One reusable palette separates material-set bindings from named object rules.
// Only placements produce entities. Surface roles are owned by MaterialSet.
// Rules are key-sorted so iteration is independent of source ordering.
struct TerrainPalette {
  static constexpr int CurrentFormatVersion = 2;
  int formatVersion = CurrentFormatVersion;
  // The asset:// id the palette was loaded from.
  std::string id;
  std::string name;
  std::map<std::string, TerrainPaletteEntry, std::less<>> placements;
  std::string materialSet;

  // Every asset:// and prefab:// this palette depends on, sorted and deduped.
  [[nodiscard]] std::vector<std::string> assetDependencies() const;
};

// Rule IDs are durable, author-chosen names, independent of surface roles.
[[nodiscard]] bool validTerrainPlacementRuleId(std::string_view id);
[[nodiscard]] std::uint64_t terrainPlacementRuleHash(std::string_view id);
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
