#include "demi/runtime/render/Renderer3DBatcher.h"
#include "demi/runtime/render/Renderer3DInternal.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <iostream>

namespace {

demi::runtime::Entity makeModel(const std::string &id, const float alpha) {
  demi::runtime::Entity entity;
  entity.id = id;
  entity.setComponent(demi::runtime::MeshRendererComponent{
      .model = "asset://models/crate", .color = {1.0F, 1.0F, 1.0F, alpha}});
  return entity;
}

} // namespace

int main() {
  using namespace demi::runtime;
  Entity first = makeModel("first", 1.0F);
  Entity second = makeModel("second", 1.0F);
  Entity transparent = makeModel("transparent", 0.5F);
  Entity animated = makeModel("animated", 1.0F);
  animated.setComponent(AnimationPlayer3DComponent{});
  const auto batches =
      buildRenderBatches3D({&first, &second, &transparent, &animated});
  const bool hasStaticBatch =
      std::ranges::any_of(batches, [](const RenderBatch3D &batch) {
        return batch.entities.size() == 2;
      });
  if (batches.size() != 3 || !hasStaticBatch ||
      batches.back().key != "transparent" ||
      batches.back().entities.front()->id != "transparent") {
    std::cerr << "Renderer3D did not form deterministic material batches.\n";
    return 1;
  }

  World world;
  Entity visible = makeModel("visible", 1.0F);
  visible.setComponent(Transform3DComponent{.position = {0.0F, 0.0F, 10.0F}});
  world.entities.push_back(std::move(visible));
  Camera3DComponent camera;
  camera.targetOffset = {0.0F, 0.0F, 1.0F};
  camera.nearClip = 0.1F;
  camera.farClip = 100.0F;
  if (!renderer3d_detail::meshEntityVisible(
          world, world.entities[0],
          *world.entities[0].component<MeshRendererComponent>(), {},
          {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, camera, 16.0F / 9.0F)) {
    std::cerr << "Renderer3D culled a visible mesh.\n";
    return 1;
  }
  world.entities[0].component<Transform3DComponent>()->position.z = 200.0F;
  if (renderer3d_detail::meshEntityVisible(
          world, world.entities[0],
          *world.entities[0].component<MeshRendererComponent>(), {},
          {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, camera, 16.0F / 9.0F)) {
    std::cerr << "Renderer3D did not apply its far clip plane.\n";
    return 1;
  }

  auto *visibleMesh = world.entities[0].component<MeshRendererComponent>();
  visibleMesh->hasBounds = true;
  visibleMesh->boundsMin = {-0.5F, -0.5F, -0.5F};
  visibleMesh->boundsMax = {0.5F, 0.5F, 0.5F};
  visibleMesh->size = {20.0F, 20.0F, 20.0F};
  world.entities[0].component<Transform3DComponent>()->position.z = 105.0F;
  if (!renderer3d_detail::meshEntityVisible(
          world, world.entities[0], *visibleMesh, {}, {0.0F, 0.0F, 1.0F},
          {0.0F, 1.0F, 0.0F}, camera, 16.0F / 9.0F)) {
    std::cerr << "Renderer3D ignored model size when culling bounds.\n";
    return 1;
  }
  return 0;
}
