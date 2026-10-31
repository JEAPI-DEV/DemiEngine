#pragma once

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/terrain/TerrainScatterRuntime.h"
#include <stop_token>
#include <vector>

namespace demi::runtime {

// Native publication marker, never authored or serialized. Render copies own
// no gameplay state, prefab service instance, or collision resources.
struct TerrainScatterBatch {};

struct TerrainScatterBatchResult {
  // Indexed by the borrowed input span, avoiding a second owned ID set.
  std::vector<std::uint8_t> handled;
  TerrainScatterSyncStats statistics;
};

// Prepares pure visual collision-free prefabs once per source and publishes one
// MeshInstances3D owner per drawable part. Gameplay prefabs remain unhandled
// and flow through ordinary prefab instantiation.
[[nodiscard]] TerrainScatterBatchResult
syncTerrainScatterBatches(World &world, WorldCommandBuffer &commands,
                          std::string_view ownerId, TerrainScatterView view,
                          const TerrainPrefabServiceProvider &prefabs,
                          std::string &error, std::stop_token stop = {});

} // namespace demi::runtime
