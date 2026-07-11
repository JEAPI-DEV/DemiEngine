#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/scene/WorldQueries.h"

#include <iostream>

namespace {

demi::runtime::Entity makeDynamicBox() {
  demi::runtime::Entity entity;
  entity.id = "mover";
  entity.transform3D =
      demi::runtime::Transform3DComponent{.position = {0.0F, 0.5F, 0.0F}};
  entity.boxCollider3D =
      demi::runtime::BoxCollider3DComponent{.size = {1.0F, 1.0F, 1.0F}};
  entity.rigidbody3D = demi::runtime::Rigidbody3DComponent{
      .bodyType = "dynamic", .useGravity = false};
  return entity;
}

demi::runtime::Entity makeStaticBox() {
  demi::runtime::Entity entity;
  entity.id = "box_wall";
  entity.transform3D =
      demi::runtime::Transform3DComponent{.position = {1.25F, 0.5F, 0.0F}};
  entity.boxCollider3D =
      demi::runtime::BoxCollider3DComponent{.size = {1.0F, 1.0F, 1.0F}};
  entity.rigidbody3D = demi::runtime::Rigidbody3DComponent{.bodyType = "static",
                                                           .useGravity = false};
  return entity;
}

demi::runtime::Entity makeStaticSphere() {
  demi::runtime::Entity entity;
  entity.id = "sphere_wall";
  entity.transform3D =
      demi::runtime::Transform3DComponent{.position = {-1.25F, 0.5F, 0.0F}};
  entity.sphereCollider3D =
      demi::runtime::SphereCollider3DComponent{.radius = 0.5F};
  entity.rigidbody3D = demi::runtime::Rigidbody3DComponent{.bodyType = "static",
                                                           .useGravity = false};
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
      world, *mover, mover->transform3D->position,
      runtime::Vec3{.x = 1.0F, .y = 0.0F, .z = 2.0F});
  if (blockedByBox.x != 0.0F || blockedByBox.z != 2.0F) {
    std::cerr << "3D box collision should block only the colliding axis; "
                 "resolved to x="
              << blockedByBox.x << ", z=" << blockedByBox.z << ".\n";
    return 1;
  }

  const runtime::Vec3 blockedBySphere = runtime::resolveDynamicMove3D(
      world, *mover, mover->transform3D->position,
      runtime::Vec3{.x = -1.0F, .y = 0.0F, .z = 0.0F});
  if (blockedBySphere.x != 0.0F) {
    std::cerr << "3D sphere collision should block dynamic box movement; "
                 "resolved to x="
              << blockedBySphere.x << ".\n";
    return 1;
  }

  runtime::Entity triggerWall = makeStaticBox();
  triggerWall.id = "trigger_wall";
  triggerWall.transform3D->position.x = 0.0F;
  triggerWall.transform3D->position.z = 1.25F;
  triggerWall.boxCollider3D->isTrigger = true;
  world.entities.push_back(triggerWall);

  const runtime::Vec3 throughTrigger = runtime::resolveDynamicMove3D(
      world, *mover, mover->transform3D->position,
      runtime::Vec3{.x = 0.0F, .y = 0.0F, .z = 1.0F});
  if (throughTrigger.z != 1.0F) {
    std::cerr << "3D trigger collider should not block movement; resolved to z="
              << throughTrigger.z << ".\n";
    return 1;
  }

  runtime::Entity kinematic = makeDynamicBox();
  kinematic.id = "kinematic";
  kinematic.rigidbody3D->bodyType = "kinematic";
  const runtime::Vec3 kinematicMove = runtime::resolveDynamicMove3D(
      world, kinematic, kinematic.transform3D->position,
      runtime::Vec3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
  if (kinematicMove.x != 1.0F) {
    std::cerr << "Non-dynamic 3D body should bypass dynamic collision "
                 "resolution; resolved to x="
              << kinematicMove.x << ".\n";
    return 1;
  }

  return 0;
}
