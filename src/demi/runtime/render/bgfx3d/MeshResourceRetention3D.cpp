#include "demi/runtime/render/bgfx3d/MeshResourceRetention3D.h"

#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>

namespace demi::runtime::render {

void MeshResourceRetention3D::collect(const World &world) {
  ProfileScope scope("Renderer3D.cache_ownership");
  for (auto &[id, membership] : membership_) {
    membership = {};
  }
  for (const Entity &entity : world.entities) {
    const auto *mesh = entity.component<MeshRendererComponent>();
    if (mesh == nullptr) {
      continue;
    }
    const bool hasDents = !entityMeshDents3D(entity).empty();
    const bool needsDynamicMesh =
        !mesh->vertices.empty() ||
        entity.component<AnimationPlayer3DComponent>() != nullptr;
    if (hasDents || needsDynamicMesh) {
      membership_[entity.id] = {.dynamic = needsDynamicMesh,
                                .deformed = hasDents};
    }
  }
  std::erase_if(membership_, [](const auto &entry) {
    return !entry.second.dynamic && !entry.second.deformed;
  });
}

bool MeshResourceRetention3D::containsDynamic(const std::string &id) const {
  const auto found = membership_.find(id);
  return found != membership_.end() && found->second.dynamic;
}

bool MeshResourceRetention3D::containsDeformed(const std::string &id) const {
  const auto found = membership_.find(id);
  return found != membership_.end() && found->second.deformed;
}

void MeshResourceRetention3D::clear() { membership_.clear(); }

} // namespace demi::runtime::render
