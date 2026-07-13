#include "demi/runtime/render/Renderer3DBatcher.h"

#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {
namespace {

std::string batchKey(const Entity &entity) {
  const auto *mesh = entity.component<MeshRendererComponent>();
  if (mesh == nullptr || mesh->color.a < 1.0F || mesh->wireframe)
    return "transparent";
  const std::string animation =
      entity.hasComponent<AnimationPlayer3DComponent>() ? entity.id : "static";
  return "opaque:" + mesh->model + ':' + mesh->texture + ':' + mesh->shape +
         ':' + animation;
}

} // namespace

std::vector<RenderBatch3D>
buildRenderBatches3D(const std::vector<Entity *> &visibleEntities) {
  std::vector<std::pair<std::string, Entity *>> ordered;
  ordered.reserve(visibleEntities.size());
  for (Entity *entity : visibleEntities)
    ordered.emplace_back(batchKey(*entity), entity);
  std::ranges::stable_sort(ordered, [](const auto &left, const auto &right) {
    return left.first < right.first;
  });

  std::vector<RenderBatch3D> batches;
  for (auto &[key, entity] : ordered) {
    if (batches.empty() || batches.back().key != key)
      batches.push_back({.key = key, .entities = {}});
    batches.back().entities.push_back(entity);
  }
  return batches;
}

} // namespace demi::runtime
