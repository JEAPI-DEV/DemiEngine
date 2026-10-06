#pragma once

#include <nlohmann/json_fwd.hpp>
#include <span>
#include <string>
#include <string_view>

namespace demi::runtime {
struct World;
struct TerrainUpdate;
class RuntimePrefabService;

// Atomically updates every placement of a shared terrain asset. Staging uses
// affected chunks and changed scatter instances, not a copy of the full world.
// Failures leave terrain, scenery and prefab service state unchanged. Prefab
// placements require a configured service; unresolved placements are errors.
[[nodiscard]] bool
updateTerrainAssetWorld(World &world, std::string_view assetId,
                        const nlohmann::json &recipe,
                        const TerrainUpdate &update, std::string &error,
                        RuntimePrefabService *prefabs = nullptr);

[[nodiscard]] bool
updateTerrainWorldBatch(World &world, std::span<const std::string> ownerIds,
                        const nlohmann::json &recipe,
                        const TerrainUpdate &update, std::string &error,
                        RuntimePrefabService *prefabs = nullptr);
} // namespace demi::runtime
