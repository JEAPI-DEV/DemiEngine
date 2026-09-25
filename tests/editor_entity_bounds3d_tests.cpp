#include "editor/EditorEntityBounds3D.h"
#include "editor/EditorSceneViewState.h"
#include "editor/EditorViewportProjection.h"

#include "demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/CapsuleCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ConvexCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SphereCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <cassert>
#include <cmath>
#include <string>
#include <utility>

namespace {

bool close(const float left, const float right) {
  return std::abs(left - right) < 0.001F;
}

bool close(const demi::runtime::Vec3 left, const demi::runtime::Vec3 right) {
  return close(left.x, right.x) && close(left.y, right.y) &&
         close(left.z, right.z);
}

demi::runtime::Entity transformed(std::string id) {
  demi::runtime::Entity entity;
  entity.id = std::move(id);
  entity.setComponent(demi::runtime::Transform3DComponent{});
  return entity;
}

void expectLocalBounds(const demi::runtime::World &world,
                       const demi::runtime::Entity &entity,
                       const demi::runtime::Vec3 minimum,
                       const demi::runtime::Vec3 maximum) {
  const auto bounds = demi::editor::editorEntityBounds3D(world, entity);
  assert(bounds && bounds->isSceneGeometry);
  assert(close(bounds->local.minimum, minimum));
  assert(close(bounds->local.maximum, maximum));
}

} // namespace

int main() {
  using namespace demi;

  runtime::World primitiveWorld;
  runtime::Entity box = transformed("box");
  box.setComponent(runtime::BoxCollider3DComponent{
      .size = {4.0F, 6.0F, 8.0F}, .offset = {1.0F, 2.0F, 3.0F}});
  primitiveWorld.entities.push_back(std::move(box));
  expectLocalBounds(primitiveWorld, primitiveWorld.entities.back(),
                    {-1.0F, -1.0F, -1.0F}, {3.0F, 5.0F, 7.0F});

  runtime::Entity sphere = transformed("sphere");
  sphere.setComponent(runtime::SphereCollider3DComponent{
      .radius = 2.0F, .offset = {1.0F, 2.0F, 3.0F}});
  primitiveWorld.entities.push_back(std::move(sphere));
  expectLocalBounds(primitiveWorld, primitiveWorld.entities.back(),
                    {-1.0F, 0.0F, 1.0F}, {3.0F, 4.0F, 5.0F});

  runtime::Entity capsule = transformed("capsule");
  capsule.setComponent(runtime::CapsuleCollider3DComponent{
      .radius = 1.5F, .height = 8.0F, .offset = {1.0F, 2.0F, 3.0F}});
  primitiveWorld.entities.push_back(std::move(capsule));
  expectLocalBounds(primitiveWorld, primitiveWorld.entities.back(),
                    {-0.5F, -2.0F, 1.5F}, {2.5F, 6.0F, 4.5F});

  runtime::Entity convex = transformed("convex");
  convex.setComponent(runtime::ConvexCollider3DComponent{
      .points = {{-2.0F, 1.0F, 4.0F}, {3.0F, -5.0F, 2.0F},
                 {1.0F, 2.0F, -6.0F}},
      .offset = {1.0F, 2.0F, 3.0F}});
  primitiveWorld.entities.push_back(std::move(convex));
  expectLocalBounds(primitiveWorld, primitiveWorld.entities.back(),
                    {-1.0F, -3.0F, -3.0F}, {4.0F, 4.0F, 7.0F});

  runtime::Entity mesh = transformed("mesh");
  mesh.setComponent(runtime::MeshRendererComponent{
      .size = {-4.0F, 6.0F, 8.0F},
      .boundsMin = {-2.0F, -0.5F, -1.0F},
      .boundsMax = {1.0F, 1.5F, 2.0F},
      .hasBounds = true});
  primitiveWorld.entities.push_back(std::move(mesh));
  expectLocalBounds(primitiveWorld, primitiveWorld.entities.back(),
                    {-4.0F, -3.0F, -8.0F}, {8.0F, 9.0F, 16.0F});

  runtime::World hierarchyWorld;
  runtime::Entity parent = transformed("parent");
  *parent.component<runtime::Transform3DComponent>() = {
      .position = {10.0F, 2.0F, 3.0F},
      .rotation = {0.0F, 1.57079632679F, 0.0F},
      .scale = {2.0F, 3.0F, 4.0F}};
  hierarchyWorld.entities.push_back(std::move(parent));

  runtime::Entity child = transformed("child");
  *child.component<runtime::Transform3DComponent>() = {
      .parent = "parent",
      .position = {1.0F, 0.0F, 0.0F},
      .scale = {0.5F, 2.0F, 1.0F}};
  child.setComponent(runtime::MeshRendererComponent{
      .size = {80.0F, 2.0F, 4.0F},
      .boundsMin = {-0.5F, -0.5F, -0.5F},
      .boundsMax = {0.5F, 0.5F, 0.5F},
      .hasBounds = true});
  hierarchyWorld.entities.push_back(std::move(child));

  const auto childBounds = editor::editorEntityBounds3D(
      hierarchyWorld, hierarchyWorld.entities.back());
  assert(childBounds && childBounds->worldTransforms.size() == 1);
  const auto childWorldBounds = editor::editorWorldBounds3D(*childBounds);
  assert(childWorldBounds);
  assert(close(childWorldBounds->minimum, {2.0F, -4.0F, -39.0F}));
  assert(close(childWorldBounds->maximum, {18.0F, 8.0F, 41.0F}));

  editor::EditorSceneViewState sceneView;
  runtime::World emptyWorld;
  sceneView.reset(emptyWorld);
  assert(sceneView.frameEntity(hierarchyWorld, "child"));
  const auto framed = sceneView.camera();
  const runtime::Vec3 center{10.0F, 2.0F, 1.0F};
  const auto projected = editor::projectScenePoint3D(
      framed, center, {800.0F, 600.0F});
  assert(projected && close(projected->x, 400.0F) &&
         close(projected->y, 300.0F));
  const runtime::Vec3 cameraOffset{framed.position.x - center.x,
                                   framed.position.y - center.y,
                                   framed.position.z - center.z};
  assert(std::sqrt(cameraOffset.x * cameraOffset.x +
                   cameraOffset.y * cameraOffset.y +
                   cameraOffset.z * cameraOffset.z) > 80.0F);
  sceneView.reset(emptyWorld);
  assert(sceneView.frameScene(hierarchyWorld));
  assert(close(sceneView.camera().position, framed.position));

  runtime::World instanceWorld;
  runtime::Entity instances = transformed("instances");
  instances.setComponent(runtime::MeshRendererComponent{
      .size = {2.0F, 2.0F, 2.0F}});
  runtime::MeshInstances3DComponent instanceTransforms;
  instanceTransforms.transforms["left"].position = {-5.0F, 0.0F, 0.0F};
  instanceTransforms.transforms["right"].position = {5.0F, 0.0F, 0.0F};
  instances.setComponent(std::move(instanceTransforms));
  instanceWorld.entities.push_back(std::move(instances));
  const auto instanceBounds = editor::editorEntityBounds3D(
      instanceWorld, instanceWorld.entities.front());
  assert(instanceBounds && instanceBounds->worldTransforms.size() == 2);
  const auto instanceWorldBounds = editor::editorWorldBounds3D(*instanceBounds);
  assert(instanceWorldBounds);
  assert(close(instanceWorldBounds->minimum, {-6.0F, -1.0F, -1.0F}));
  assert(close(instanceWorldBounds->maximum, {6.0F, 1.0F, 1.0F}));

  return 0;
}
