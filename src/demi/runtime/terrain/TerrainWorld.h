#pragma once

#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>

namespace demi::runtime {

struct World;
struct TerrainUpdate;

// Prepared native changes own their allocations and retain the source field.
// Preparation reads a thread-confined world (or a worker-owned snapshot);
// publication is a main-thread operation and rejects stale preparations.
class PreparedTerrainWorldUpdate {
public:
  PreparedTerrainWorldUpdate(PreparedTerrainWorldUpdate &&) noexcept;
  PreparedTerrainWorldUpdate &operator=(PreparedTerrainWorldUpdate &&) noexcept;
  ~PreparedTerrainWorldUpdate();

private:
  struct State;
  explicit PreparedTerrainWorldUpdate(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;

  friend std::optional<PreparedTerrainWorldUpdate>
  prepareTerrainWorldUpdate(const World &, std::string_view,
                            const nlohmann::json &, const TerrainUpdate &,
                            std::string &, std::stop_token);
  friend bool publishTerrainWorldUpdate(World &, PreparedTerrainWorldUpdate &&,
                                        std::string &, std::stop_token);
};

[[nodiscard]] std::optional<PreparedTerrainWorldUpdate>
prepareTerrainWorldUpdate(const World &world, std::string_view ownerId,
                          const nlohmann::json &recipe,
                          const TerrainUpdate &update, std::string &error,
                          std::stop_token stop = {});

// Checks cancellation before committing. After that check, all native changes
// are installed together without allocations or partial visible/collision
// state.
[[nodiscard]] bool
publishTerrainWorldUpdate(World &world, PreparedTerrainWorldUpdate &&prepared,
                          std::string &error, std::stop_token stop = {});

// Local updates require the owner's retained generated field. Call before a
// generic reflected recipe mutation clears it. Rectangles come from core's
// inclusive height/normal/biome invalidation, including its computed halo.
[[nodiscard]] bool updateTerrainWorld(World &world, std::string_view ownerId,
                                      const nlohmann::json &recipe,
                                      const TerrainUpdate &update,
                                      std::string &error);

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
