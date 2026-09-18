#include "demi/runtime/scene/PrefabPlacements3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/PrefabPlacement3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include <algorithm>
#include <unordered_set>

namespace demi::runtime {
std::vector<PrefabPlacement3D>
collectPrefabPlacements3D(const World &world, const std::string_view ancestor) {
  std::vector<PrefabPlacement3D> result;
  for (const auto &entity : world.entities) {
    const auto *placement = entity.component<PrefabPlacement3DComponent>();
    if (!placement || placement->prefab.empty())
      continue;
    bool belongs = ancestor.empty();
    bool enabled = true;
    const Entity *current = &entity;
    std::unordered_set<std::string> visited;
    while (current) {
      if (!visited.insert(current->id).second || !current->enabled) {
        enabled = false;
        break;
      }
      belongs = belongs || current->id == ancestor;
      const auto *transform = current->component<Transform3DComponent>();
      if (!transform || transform->parent.empty())
        break;
      current = findEntity(world, transform->parent);
      if (!current)
        enabled = false;
    }
    if (enabled && belongs)
      if (auto transform = resolveWorldTransform3D(world, entity))
        result.push_back({entity.id, placement->prefab, placement->root,
                          *transform, placement->preserve});
  }
  std::ranges::sort(result, {}, &PrefabPlacement3D::id);
  return result;
}
} // namespace demi::runtime
