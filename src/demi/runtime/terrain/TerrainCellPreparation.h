#pragma once
#include "demi/runtime/terrain/PreparedTerrainCell.h"
#include "demi/runtime/terrain/TerrainScatterPlacement.h"
#include <filesystem>
#include <span>
#include <stop_token>

namespace demi::runtime {
struct World;
class WorldCommandBuffer;
// Worker-local prefab resolution and the same batching implementation used by
// finite terrain. Prepares remaining prefabs without registering live
// ownership.
[[nodiscard]] std::shared_ptr<const PreparedTerrainCell>
prepareTerrainCell(std::vector<TerrainScatterPlacement> &placements,
                   std::string_view ownerId, std::string_view paletteId,
                   const std::filesystem::path &projectDirectory,
                   std::stop_token stop);
// Main-thread adoption; filters authored exclusions and retires obsolete batch
// owners. Interactive prefab ownership remains with TerrainScatterRuntime.
void publishTerrainVisuals(World &world, WorldCommandBuffer &commands,
                           const Entity &owner, const PreparedTerrainCell &cell,
                           std::span<const std::string> exclusions);
} // namespace demi::runtime
