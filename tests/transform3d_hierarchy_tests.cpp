#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/World.h"

#include <cmath>
#include <iostream>
#include <numbers>

namespace {

using namespace demi::runtime;

bool close(const float left, const float right) {
  return std::abs(left - right) < 0.0001F;
}

bool closeDirection(const float left, const float right) {
  return std::abs(left - right) < 0.001F;
}

Entity transformed(std::string id, Transform3DComponent transform) {
  Entity entity;
  entity.id = std::move(id);
  entity.setComponent(std::move(transform));
  return entity;
}

} // namespace

int main() {
  using namespace demi::runtime;
  World world;
  world.entities.push_back(transformed(
      "root", {.position = {10.0F, 0.0F, 0.0F},
               .rotation = {0.0F, 0.0F, std::numbers::pi_v<float> * 0.5F},
               .scale = {2.0F, 3.0F, 4.0F}}));
  world.entities.push_back(transformed(
      "child", {.parent = "root",
                .position = {1.0F, 0.0F, 0.0F},
                .rotation = {0.0F, std::numbers::pi_v<float> * 0.5F, 0.0F},
                .scale = {0.5F, 0.5F, 0.5F}}));
  world.entities.push_back(transformed(
      "grandchild", {.parent = "child", .position = {0.0F, 0.0F, 1.0F}}));

  const auto child = resolveWorldTransform3D(world, world.entities[1]);
  const auto grandchild = resolveWorldTransform3D(world, world.entities[2]);
  if (!child || !grandchild || !close(child->position.x, 10.0F) ||
      !close(child->position.y, 2.0F) || !close(child->position.z, 0.0F) ||
      !close(child->scale.x, 1.0F) || !close(child->scale.y, 1.5F) ||
      !close(child->scale.z, 2.0F) || !close(grandchild->position.x, 10.0F) ||
      !close(grandchild->position.y, 4.0F) ||
      !close(grandchild->position.z, 0.0F)) {
    std::cerr << "Transform3D hierarchy did not compose position, rotation, "
                 "and scale.\n";
    return 1;
  }

  const auto childLocal =
      worldToLocalTransform3D(world, world.entities[1], *child);
  const auto roundTripped =
      childLocal
          ? resolveWorldTransform3D(world, world.entities[1], *childLocal)
          : std::nullopt;
  const Vec3 childForward = forwardDirection3D(*child);
  const Vec3 roundTrippedForward =
      roundTripped ? forwardDirection3D(*roundTripped) : Vec3{};
  if (!childLocal || !roundTripped ||
      !close(roundTripped->position.x, child->position.x) ||
      !close(roundTripped->position.y, child->position.y) ||
      !close(roundTripped->position.z, child->position.z) ||
      !closeDirection(roundTrippedForward.x, childForward.x) ||
      !closeDirection(roundTrippedForward.y, childForward.y) ||
      !closeDirection(roundTrippedForward.z, childForward.z) ||
      !close(roundTripped->scale.x, child->scale.x)) {
    std::cerr << "World-to-local conversion did not invert a rotated, scaled "
                 "parent.\n";
    if (childLocal)
      std::cerr << "local position=" << childLocal->position.x << ','
                << childLocal->position.y << ',' << childLocal->position.z
                << " rotation=" << childLocal->rotation.x << ','
                << childLocal->rotation.y << ',' << childLocal->rotation.z
                << " scale=" << childLocal->scale.x << ','
                << childLocal->scale.y << ',' << childLocal->scale.z << '\n';
    if (roundTripped)
      std::cerr << "world position=" << roundTripped->position.x << ','
                << roundTripped->position.y << ',' << roundTripped->position.z
                << " forward=" << roundTrippedForward.x << ','
                << roundTrippedForward.y << ',' << roundTrippedForward.z
                << '\n';
    std::cerr << "expected position=" << child->position.x << ','
              << child->position.y << ',' << child->position.z
              << " forward=" << childForward.x << ',' << childForward.y << ','
              << childForward.z << '\n';
    return 1;
  }

  const Vec3 localQuarterTurn = rotateLocalEuler3D(
      {}, {0.0F, 1.0F, 0.0F}, std::numbers::pi_v<float> * 0.5F);
  const Vec3 localForward = forwardDirection3D(
      {.position = {}, .rotation = localQuarterTurn, .scale = {1, 1, 1}});
  const Vec3 worldQuarterTurn = rotateWorldEuler3D(
      {}, {1.0F, 0.0F, 0.0F}, std::numbers::pi_v<float> * 0.5F);
  const Vec3 worldUp = upDirection3D(
      {.position = {}, .rotation = worldQuarterTurn, .scale = {1, 1, 1}});
  if (!closeDirection(localForward.x, 1.0F) ||
      !closeDirection(localForward.z, 0.0F) ||
      !closeDirection(worldUp.z, 1.0F)) {
    std::cerr << "Local/world axis rotation composition is incorrect.\n";
    return 1;
  }

  auto *rootTransform = world.entities[0].component<Transform3DComponent>();
  rootTransform->scale.x = 0.0F;
  if (worldToLocalTransform3D(world, world.entities[1], *child)) {
    std::cerr << "World-to-local conversion accepted a singular parent.\n";
    return 1;
  }
  rootTransform->scale.x = 2.0F;

  World cameraWorld;
  cameraWorld.entities.push_back(transformed(
      "camera_parent",
      {.rotation = {0.0F, std::numbers::pi_v<float> * 0.75F, 0.0F}}));
  Entity camera = transformed("camera", {.parent = "camera_parent"});
  camera.setComponent(
      Camera3DComponent{.targetOffset = {0.0F, 0.0F, -1.0F}, .upAxis = 1.0F});
  cameraWorld.entities.push_back(std::move(camera));
  const Vec3 forward = activeCamera3DForward(cameraWorld);
  const Vec3 up = activeCamera3DUp(cameraWorld);
  if (!close(forward.x, -0.7071068F) || !close(forward.y, 0.0F) ||
      !close(forward.z, 0.7071068F) || !close(up.x, 0.0F) ||
      !close(up.y, 1.0F) || !close(up.z, 0.0F)) {
    std::cerr << "Camera direction did not preserve composed rotation past "
                 "the Euler quarter-turn.\n";
    return 1;
  }

  world.entities[0].component<Transform3DComponent>()->parent = "grandchild";
  const auto cycleIssues = validateTransform3DHierarchy(world);
  if (cycleIssues.empty() ||
      cycleIssues.front().kind != Transform3DHierarchyIssueKind::Cycle ||
      resolveWorldTransform3D(world, world.entities[2])) {
    std::cerr << "Transform3D hierarchy cycle was not detected safely.\n";
    return 1;
  }

  world.entities[0].component<Transform3DComponent>()->parent.clear();
  world.entities[1].component<Transform3DComponent>()->parent = "missing";
  const auto missingIssues = validateTransform3DHierarchy(world);
  if (missingIssues.empty() ||
      missingIssues.front().kind !=
          Transform3DHierarchyIssueKind::MissingParent) {
    std::cerr << "Missing Transform3D parent was not diagnosed.\n";
    return 1;
  }
  return 0;
}
