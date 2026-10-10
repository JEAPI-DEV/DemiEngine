#pragma once

#include "demi/runtime/scene/RuntimePrefabService.h"
#include "demi/runtime/scene/WorldCommandBuffer.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/terrain/TerrainScatterPlacement.h"
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace demi::runtime {

struct World;
struct HeightField;

// The stable identity of one scattered instance.
//
// Reconciliation keys on the cell that owns the placement, not on its
// position, so a sculpt stroke that moves the ground under an instance updates
// that instance instead of destroying and rebuilding it. The rule and palette
// are part of the key so two roles landing in the same cell, or a palette swap,
// never collide.
[[nodiscard]] std::string terrainScatterInstanceId(std::string_view ownerId,
                                                   std::string_view paletteId,
                                                   std::string_view ruleId,
                                                   std::size_t cell);

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
// A placement with a prefab is instantiated through RuntimePrefabService, so
// templating and release behave like authored PrefabPlacement3D. Scatter keeps
// one stable instance id per cell rather than reusing another cell's pool id.
// A placement with only an asset gets a plain Transform3D +
// MeshRenderer entity. Palette validation requires that direct asset to be a
// Model3D; a material-only asset needs a prefab that supplies geometry.
//
// The service is optional. Passing null skips prefab placements and counts them
// as unresolved rather than guessing, so a caller that only has mesh assets
// needs no prefab owner. The runtime currently has exactly one RuntimePrefabService
// and it belongs to the scripting host, so a composition root must supply one
// before prefab-backed roles can place.
[[nodiscard]] TerrainScatterSyncStats
syncTerrainScatter(World &world, WorldCommandBuffer &commands,
                   std::string_view ownerId, const HeightField &field,
                   RuntimePrefabService *prefabs, std::string &error);

// Resolve mutable prefab bookkeeping only when creating/releasing an instance.
// Transactions can lazily fork that state; moving/retaining existing instances
// and direct-mesh placements do not need a copy of the prefab service.
using TerrainPrefabServiceProvider = std::function<RuntimePrefabService *()>;
[[nodiscard]] TerrainScatterSyncStats
syncTerrainScatter(World &world, WorldCommandBuffer &commands,
                   std::string_view ownerId, const HeightField &field,
                   const TerrainPrefabServiceProvider &prefabs,
                   std::string &error);

// Removes every scattered instance belonging to an owner, used when a terrain
// is disabled, unloaded, or loses its palette.
[[nodiscard]] std::size_t releaseTerrainScatter(World &world,
                                                 WorldCommandBuffer &commands,
                                                 std::string_view ownerId);

} // namespace demi::runtime
