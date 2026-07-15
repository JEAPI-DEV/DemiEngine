#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/physics/SpatialQuery3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <iostream>

namespace {

using namespace demi::runtime;

demi::runtime::Entity makeDynamicBox() {
  demi::runtime::Entity entity;
  entity.id = "mover";
  entity.setComponent<Transform3DComponent>(
      demi::runtime::Transform3DComponent{.position = {0.0F, 0.5F, 0.0F}});
  entity.setComponent<BoxCollider3DComponent>(
      demi::runtime::BoxCollider3DComponent{.size = {1.0F, 1.0F, 1.0F}});
  entity.setComponent<Rigidbody3DComponent>(demi::runtime::Rigidbody3DComponent{
      .bodyType = "dynamic", .useGravity = false});
  return entity;
}

demi::runtime::Entity makeStaticBox() {
  demi::runtime::Entity entity;
  entity.id = "box_wall";
  entity.setComponent<Transform3DComponent>(
      demi::runtime::Transform3DComponent{.position = {1.25F, 0.5F, 0.0F}});
  entity.setComponent<BoxCollider3DComponent>(
      demi::runtime::BoxCollider3DComponent{.size = {1.0F, 1.0F, 1.0F}});
  entity.setComponent<Rigidbody3DComponent>(demi::runtime::Rigidbody3DComponent{
      .bodyType = "static", .useGravity = false});
  return entity;
}

demi::runtime::Entity makeStaticSphere() {
  demi::runtime::Entity entity;
  entity.id = "sphere_wall";
  entity.setComponent<Transform3DComponent>(
      demi::runtime::Transform3DComponent{.position = {-1.25F, 0.5F, 0.0F}});
  entity.setComponent<SphereCollider3DComponent>(
      demi::runtime::SphereCollider3DComponent{.radius = 0.5F});
  entity.setComponent<Rigidbody3DComponent>(demi::runtime::Rigidbody3DComponent{
      .bodyType = "static", .useGravity = false});
  return entity;
}

} // namespace

int main() {
  namespace runtime = demi::runtime;

  runtime::World world;
  world.entities.push_back(makeDynamicBox());
  world.entities.push_back(makeStaticBox());
  world.entities.push_back(makeStaticSphere());

  const runtime::Entity *mover = runtime::findEntity(world, "mover");
  if (mover == nullptr) {
    std::cerr << "3D mover was not available.\n";
    return 1;
  }

  const runtime::Vec3 blockedByBox = runtime::resolveDynamicMove3D(
      world, *mover, mover->component<Transform3DComponent>()->position,
      runtime::Vec3{.x = 1.0F, .y = 0.0F, .z = 2.0F});
  if (blockedByBox.x != 0.0F || blockedByBox.z != 2.0F) {
    std::cerr << "3D box collision should block only the colliding axis; "
                 "resolved to x="
              << blockedByBox.x << ", z=" << blockedByBox.z << ".\n";
    return 1;
  }

  const runtime::Vec3 blockedBySphere = runtime::resolveDynamicMove3D(
      world, *mover, mover->component<Transform3DComponent>()->position,
      runtime::Vec3{.x = -1.0F, .y = 0.0F, .z = 0.0F});
  if (blockedBySphere.x != 0.0F) {
    std::cerr << "3D sphere collision should block dynamic box movement; "
                 "resolved to x="
              << blockedBySphere.x << ".\n";
    return 1;
  }

  runtime::Entity triggerWall = makeStaticBox();
  triggerWall.id = "trigger_wall";
  triggerWall.component<Transform3DComponent>()->position.x = 0.0F;
  triggerWall.component<Transform3DComponent>()->position.z = 1.25F;
  triggerWall.component<BoxCollider3DComponent>()->isTrigger = true;
  world.entities.push_back(triggerWall);
  mover = runtime::findEntity(world, "mover");
  if (mover == nullptr) {
    std::cerr << "3D mover was not available after adding a trigger.\n";
    return 1;
  }

  const runtime::Vec3 throughTrigger = runtime::resolveDynamicMove3D(
      world, *mover, mover->component<Transform3DComponent>()->position,
      runtime::Vec3{.x = 0.0F, .y = 0.0F, .z = 1.0F});
  if (throughTrigger.z != 1.0F) {
    std::cerr << "3D trigger collider should not block movement; resolved to z="
              << throughTrigger.z << ".\n";
    return 1;
  }

  runtime::Entity generatedCollider;
  generatedCollider.id = "generated_model_wall";
  generatedCollider.setComponent<runtime::Transform3DComponent>(
      runtime::Transform3DComponent{.position = {0.0F, 0.5F, 1.25F}});
  generatedCollider.setComponent<runtime::ModelCollider3DComponent>(
      runtime::ModelCollider3DComponent{.asset = "asset://colliders/test"});
  generatedCollider.setComponent<runtime::Rigidbody3DComponent>(
      runtime::Rigidbody3DComponent{.bodyType = "static", .useGravity = false});
  world.colliderAssets3D["asset://colliders/test"] =
      runtime::ColliderAsset3D{.size = {1.0F, 1.0F, 1.0F},
                               .detail = 1.0F,
                               .triangles = {{{.a = {-0.5F, -0.5F, 0.0F},
                                               .b = {0.5F, -0.5F, 0.0F},
                                               .c = {-0.5F, 0.5F, 0.0F}},
                                              {.a = {0.5F, -0.5F, 0.0F},
                                               .b = {0.5F, 0.5F, 0.0F},
                                               .c = {-0.5F, 0.5F, 0.0F}}}}};
  world.entities.push_back(generatedCollider);
  mover = runtime::findEntity(world, "mover");
  if (mover == nullptr) {
    std::cerr << "3D mover was not available after adding a collider asset.\n";
    return 1;
  }
  const runtime::Vec3 blockedByGeneratedCollider =
      runtime::resolveDynamicMove3D(
          world, *mover, mover->component<Transform3DComponent>()->position,
          runtime::Vec3{.x = 0.0F, .y = 0.0F, .z = 1.0F});
  if (blockedByGeneratedCollider.z != 0.0F) {
    std::cerr << "Generated Collider3D asset should block dynamic movement; "
                 "resolved to z="
              << blockedByGeneratedCollider.z << ".\n";
    return 1;
  }

  runtime::Entity kinematic = makeDynamicBox();
  kinematic.id = "kinematic";
  kinematic.component<Rigidbody3DComponent>()->bodyType = "kinematic";
  const runtime::Vec3 kinematicMove = runtime::resolveDynamicMove3D(
      world, kinematic, kinematic.component<Transform3DComponent>()->position,
      runtime::Vec3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
  if (kinematicMove.x != 1.0F) {
    std::cerr << "Non-dynamic 3D body should bypass dynamic collision "
                 "resolution; resolved to x="
              << kinematicMove.x << ".\n";
    return 1;
  }

  const auto overlaps = runtime::overlapSphere3D(
      world, {.x = 1.25F, .y = 0.5F, .z = 0.0F}, 0.6F, "mover");
  const auto hit =
      runtime::raycast3D(world, {.x = 0.0F, .y = 0.5F, .z = 0.0F},
                         {.x = 1.0F, .y = 0.0F, .z = 0.0F}, 4.0F, "mover");
  if (overlaps != std::vector<std::string>{"box_wall"} || !hit ||
      hit->entityId != "box_wall" || hit->distance <= 0.0F) {
    std::cerr << "Reusable 3D overlap or raycast query failed.\n";
    return 1;
  }

  return 0;
}
