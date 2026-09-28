#pragma once

#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace demi::runtime {

struct World;

// Editor picking can map a native generated surface to its authored owner.
[[nodiscard]] std::optional<std::string_view>
terrainSurfaceOwner(const Entity &entity);

// Triangle-list geometry in terrain-local coordinates. Each biome group has
// its own tint; collision consumes exactly the same positions as rendering.
[[nodiscard]] std::optional<Entity>
buildTerrainMeshEntity(const Entity &owner, std::string id,
                       std::span<const Vec3> vertices,
                       std::span<const Vec3> normals, std::span<const Vec2> uvs,
                       Color color, std::string material, std::string &error);

// Materializes native-only generated children without changing authored data.
// Failure leaves the previous generated children and retained fields intact.
[[nodiscard]] bool materializeTerrains(World &world, std::string &error);

// Refresh native child visibility before physics/rendering after gameplay
// edits.
void synchronizeTerrainVisibility(World &world);

// Scene merge/unload operations rebuild transient ownership after moving
// entities.
void rebuildTerrainOwnership(World &world);

} // namespace demi::runtime
