#pragma once

#include "demi/runtime/scene/RuntimePrefabService.h"
#include "demi/runtime/scene/WorldCommandBuffer.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/terrain/TerrainScatterPlacement.h"
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace demi::runtime {

struct World;
struct HeightField;
struct PreparedTerrainCell;

// A borrowed publication view avoids copying a whole field (or its placements)
// when a streaming cell already owns the generated records.
struct TerrainScatterView {
  std::string_view paletteId;
  std::span<const TerrainScatterPlacement> placements;
  std::span<const std::string> exclusions;
  const PreparedTerrainCell *prepared = nullptr;
};

// The stable identity of one scattered instance.
//
// Reconciliation keys on the world-space candidate lattice, not on sample
// indices or final position, so moving the ground under an instance updates
// that instance instead of destroying and rebuilding it. The rule and palette
// are part of the key so two roles sharing a candidate, or a palette swap,
// never collide.
[[nodiscard]] std::string terrainScatterInstanceId(std::string_view ownerId,
                                                   std::string_view paletteId,
                                                   std::string_view ruleId,
                                                   int candidateX,
                                                   int candidateZ);

struct TerrainScatterSyncStats {
  std::size_t created = 0;
  std::size_t updated = 0;
  std::size_t removed = 0;
  // Placements whose instance already matched exactly and was left alone.
  std::size_t retained = 0;
  std::size_t prefabInstances = 0;
  // Placements whose asset could not be resolved. They are reported rather than
  // dropped, because a silently missing prop is indistinguishable from a bug.
  std::size_t unresolved = 0;

  [[nodiscard]] std::size_t total() const noexcept {
    return created + updated + removed + retained;
  }
};

// Reconciles the placements a field produced into native children of the
// terrain owner, and removes any instance the new field no longer produces.
//
// This is the consumer half of scattering, and it is deliberately separate from
// generation: generation decides and records where instances belong, without a
// world or a renderer. This pass turns those records into entities. It runs
// outside the allocation-free terrain publish, because instantiating a prefab
// allocates.
//
// Collision-free Model3D placements and pure visual prefabs publish shared
// MeshInstances3D owners, with stable placement keys inside their transforms.
// Prefab material/LOD settings and authored hierarchy offsets survive batching.
// No gameplay entity or physics body is allocated per visual copy.
//
// Prefabs with gameplay behavior retain ordinary RuntimePrefabService
// ownership. Collision policies preserve authored collider shapes:
// static/trigger freeze bodies and select contact behavior, while none removes
// physics participation. Direct models require collision:none in authored
// palettes; explicit collider geometry belongs in a prefab.
//
// The service is optional. Passing null skips prefab placements and counts them
// as unresolved rather than guessing, so a caller that only has mesh assets
// needs no prefab owner. The runtime currently has exactly one
// RuntimePrefabService and it belongs to the scripting host, so a composition
// root must supply one before prefab-backed roles can place.
[[nodiscard]] TerrainScatterSyncStats
syncTerrainScatter(World &world, WorldCommandBuffer &commands,
                   std::string_view ownerId, const HeightField &field,
                   RuntimePrefabService *prefabs, std::string &error);

// Transactions resolve prefab state lazily for template preparation or live
// instance creation/release. Direct model batches require no prefab service.
using TerrainPrefabServiceProvider = std::function<RuntimePrefabService *()>;
[[nodiscard]] TerrainScatterSyncStats
syncTerrainScatter(World &world, WorldCommandBuffer &commands,
                   std::string_view ownerId, const HeightField &field,
                   const TerrainPrefabServiceProvider &prefabs,
                   std::string &error);

[[nodiscard]] TerrainScatterSyncStats
syncTerrainScatter(World &world, WorldCommandBuffer &commands,
                   std::string_view ownerId, TerrainScatterView view,
                   const TerrainPrefabServiceProvider &prefabs,
                   std::string &error);

// Publishes only the interactive/unbatched records after visual preparation.
// Render-batch ownership is managed by the caller's prepared page publication.
[[nodiscard]] TerrainScatterSyncStats
syncTerrainScatterEntities(World &world, WorldCommandBuffer &commands,
                           std::string_view ownerId, TerrainScatterView view,
                           const TerrainPrefabServiceProvider &prefabs,
                           std::string &error);

// Removes every scattered instance belonging to an owner, used when a terrain
// is disabled, unloaded, or loses its palette.
[[nodiscard]] std::size_t releaseTerrainScatter(World &world,
                                                WorldCommandBuffer &commands,
                                                std::string_view ownerId);

} // namespace demi::runtime
