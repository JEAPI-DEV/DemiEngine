#pragma once

#include "demi/runtime/scene/RuntimePrefabService.h"
#include <cstddef>
#include <map>
#include <vector>

namespace demi::runtime {
// Immutable scene data prepared without touching the live world.
// MeshInstances3D owns shared transform storage, so main-thread adoption does
// not reconstruct per-instance labels or transforms.
struct PreparedTerrainCell {
  std::vector<Entity> batches;
  std::map<std::string, PreparedPrefabInstance, std::less<>> prefabInstances;
  // Retained instance/geometry/string payload estimate; excludes shared assets,
  // ECS registry overhead, authoring metadata and allocator bookkeeping.
  // Charged with the cell's preparation reserve.
  std::size_t accountedBytes = 0;
};
} // namespace demi::runtime
