#pragma once
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include <string_view>
#include <vector>

namespace demi::runtime {
struct PrefabPlacement3D {
  std::string id, prefab, root;
  WorldTransform3D transform;
  bool preserve = true;
};
// Enabled placements below an optional ancestor, sorted by stable ID. Reading
// placements never instantiates bodies or executes scripts.
[[nodiscard]] std::vector<PrefabPlacement3D>
collectPrefabPlacements3D(const World &world, std::string_view ancestor = {});
} // namespace demi::runtime
