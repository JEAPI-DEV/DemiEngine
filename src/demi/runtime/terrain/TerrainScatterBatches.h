#pragma once

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/terrain/TerrainScatterRuntime.h"
#include <unordered_set>

namespace demi::runtime {

// Native publication marker, never authored or serialized. Render copies own
// no gameplay state, prefab service instance, or collision resources.
struct TerrainScatterBatch {};

struct TerrainScatterBatchResult {
  std::unordered_set<std::string> placements;
  TerrainScatterSyncStats statistics;
};

// Prepares pure visual collision-free prefabs once per source and publishes one
// MeshInstances3D owner per drawable part. Gameplay prefabs remain unhandled
// and flow through ordinary prefab instantiation.
[[nodiscard]] TerrainScatterBatchResult
syncTerrainScatterBatches(World &world, WorldCommandBuffer &commands,
                          std::string_view ownerId, const HeightField &field,
                          const TerrainPrefabServiceProvider &prefabs,
                          std::string &error);

} // namespace demi::runtime
