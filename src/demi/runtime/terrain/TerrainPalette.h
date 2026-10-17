#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
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

namespace demi::runtime {

// What a scattered instance contributes to the physics world. None leaves the
// decoration pass entirely, Trigger is enterable but immovable, Static is a
// full blocker.
enum class TerrainCollisionPolicy { None, Static, Trigger };

enum class TerrainScatterCategory { Prop, Grass, Tree, Rock };

struct TerrainScatterRange {
  float minimum = 0;
  float maximum = std::numeric_limits<float>::max();

  [[nodiscard]] bool contains(float value) const noexcept {
    return value >= minimum && value <= maximum;
  }
};

struct TerrainPaletteEntry {
  std::string model;
  std::string prefab;
  // Expected instances per square terrain-local unit before spacing/clustering.
  float density = 1;
  float candidateSpacing = 0;
  [[nodiscard]] float candidateStep() const noexcept {
    if (candidateSpacing > 0.F) {
      return candidateSpacing;
    }
    if (spacing > 0.F) {
      return spacing * 0.5F;
    }
    return 0.25F;
  }
  TerrainScatterCategory category = TerrainScatterCategory::Prop;
  TerrainScatterRange slope{0, 90};
  TerrainScatterRange moisture{0, 1};
  TerrainScatterRange waterDistance;
  TerrainScatterRange yawDegrees{0, 360};
  // A smooth terrain-local density field; zero strength leaves uniform scatter.
  float clusterSize = 16;
  float clusterStrength = 0;
  float scaleMin = 1;
  float scaleMax = 1;
  // World units between instances. 0 leaves placement unconstrained, which is
  // what continuous ground cover needs and clumped props must not use.
  float spacing = 0;
  TerrainCollisionPolicy collision = TerrainCollisionPolicy::Static;
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
  static constexpr int CurrentFormatVersion = 3;
  int formatVersion = CurrentFormatVersion;
  // The asset:// id the palette was loaded from.
  std::string id;
  std::string name;
  float artDensity = 1;
  float grassDensity = 1;
  float treeDensity = 1;
  float rockDensity = 1;

  [[nodiscard]] float
  densityFor(TerrainScatterCategory category) const noexcept;
  std::map<std::string, TerrainPaletteEntry, std::less<>> placements;
  std::string materialSet;
  // Resolved native snapshot; not a second authored binding map.
  std::map<std::string, std::string, std::less<>> surfaceMaterials;

  // Every asset:// and prefab:// this palette depends on, sorted and deduped.
  [[nodiscard]] std::vector<std::string> assetDependencies() const;
};

// Rule IDs are durable, author-chosen names, independent of surface roles.
[[nodiscard]] bool validTerrainPlacementRuleId(std::string_view id);
[[nodiscard]] std::uint64_t terrainPlacementRuleHash(std::string_view id);
[[nodiscard]] std::string_view
terrainScatterCategoryName(TerrainScatterCategory category);
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
