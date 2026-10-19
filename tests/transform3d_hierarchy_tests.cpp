#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldTransformSnapshot3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/World.h"

#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <utility>

namespace {

using namespace demi::runtime;

bool close(const float left, const float right) {
  return std::abs(left - right) < 0.0001F;
}

bool closeDirection(const float left, const float right) {
  return std::abs(left - right) < 0.001F;
}

bool closeVector(const Vec3 left, const Vec3 right) {
  return close(left.x, right.x) && close(left.y, right.y) &&
         close(left.z, right.z);
}

bool checkTransformBasis() {
  const std::array<Vec3, 4> rotations{
      Vec3{}, Vec3{0.37F, -0.91F, 1.23F},
      Vec3{0.4F, std::numbers::pi_v<float> * 0.5F, -0.7F},
      Vec3{-2.3F, 2.1F, 4.7F}};
  const std::array<Vec3, 3> scales{
      Vec3{2.0F, 3.0F, 0.5F}, Vec3{-2.0F, 0.25F, -4.0F},
      Vec3{0.0F, -3.0F, 1.5F}};
  for (const auto rotation : rotations) {
    for (const auto scale : scales) {
      const WorldTransform3D transform{
          .position = {3.0F, -7.0F, 2.0F},
          .rotation = rotation,
          .scale = scale};
      const auto basis = transformBasis3D(transform);
      const auto expectedRight =
          transformDirection3D(transform, {scale.x, 0.0F, 0.0F});
      const auto expectedUp =
          transformDirection3D(transform, {0.0F, scale.y, 0.0F});
      const auto expectedForward =
          transformDirection3D(transform, {0.0F, 0.0F, scale.z});
      const Vec3 point{0.7F, -1.2F, 2.3F};
      const Vec3 projected{
          transform.position.x + basis.right.x * point.x +
              basis.up.x * point.y + basis.forward.x * point.z,
          transform.position.y + basis.right.y * point.x +
              basis.up.y * point.y + basis.forward.y * point.z,
          transform.position.z + basis.right.z * point.x +
              basis.up.z * point.y + basis.forward.z * point.z};
      if (!closeVector(basis.right, expectedRight) ||
          !closeVector(basis.up, expectedUp) ||
          !closeVector(basis.forward, expectedForward) ||
          !closeVector(projected, transformPoint3D(transform, point))) {
        return false;
      }
    }
  }
  return true;
}

bool checkUnrotatedParentComposition() {
  const WorldTransform3D parent{
      .position = {4.0F, -3.0F, 8.0F}, .scale = {-2.0F, 3.0F, 0.25F}};
  const WorldTransform3D local{.position = {2.0F, -1.0F, 4.0F},
                               .rotation = {0.37F, 2.1F, -4.2F},
                               .scale = {0.5F, -2.0F, 4.0F}};
  const auto composed = composeWorldTransform3D(parent, local);
  if (!closeVector(composed.position, {0.0F, -6.0F, 9.0F}) ||
      !closeVector(composed.scale, {-1.0F, -6.0F, 1.0F}) ||
      !closeVector(composed.rotation, local.rotation)) {
    return false;
  }
  // All three orientation axes must survive, including Euler angles outside
  // the canonical range and reflected/nonuniform scale.
  return closeVector(rightDirection3D(composed), rightDirection3D(local)) &&
         closeVector(upDirection3D(composed), upDirection3D(local)) &&
         closeVector(forwardDirection3D(composed), forwardDirection3D(local));
}

Entity transformed(std::string id, Transform3DComponent transform) {
  Entity entity;
  entity.id = std::move(id);
  entity.setComponent(std::move(transform));
  return entity;
}

bool snapshotMatchesWorld(WorldTransformSnapshot3D &snapshot,
                          const World &world) {
  const auto &results = snapshot.refresh(world);
  if (results.size() != world.entities.size()) {
    return false;
  }
  for (std::size_t index = 0; index < results.size(); ++index) {
    const auto expected = resolveWorldTransform3D(world, world.entities[index]);
    const auto &actual = results[index];
    if (actual.has_value() != expected.has_value()) {
      return false;
    }
    if (!actual) {
      continue;
    }
    if (!closeVector(actual->position, expected->position) ||
        !closeVector(actual->scale, expected->scale) ||
        !closeVector(rightDirection3D(*actual), rightDirection3D(*expected)) ||
        !closeVector(upDirection3D(*actual), upDirection3D(*expected)) ||
        !closeVector(forwardDirection3D(*actual), forwardDirection3D(*expected))) {
      return false;
    }
  }
  return true;
}

bool checkRetainedSnapshot() {
  World world;
  world.entities.push_back(transformed(
      "parent", {.position = {4, 1, 2}, .scale = {-2, 3, 0.5F}}));
  world.entities.push_back(transformed(
      "child", {.parent = "parent", .position = {1, 2, 3},
                .rotation = {0.3F, -0.7F, 1.2F}}));
  WorldTransformSnapshot3D snapshot;
  if (!snapshotMatchesWorld(snapshot, world)) {
    return false;
  }
  const auto *retained = snapshot.refresh(world).data();
  if (!snapshotMatchesWorld(snapshot, world) ||
      snapshot.refresh(world).data() != retained) {
    return false;
  }
  auto *parent = world.entities[0].component<Transform3DComponent>();
  parent->position = {-3, 2, 7};
  if (!snapshotMatchesWorld(snapshot, world)) {
    return false;
  }
  parent->rotation = {0.6F, -0.2F, 0.9F};
  if (!snapshotMatchesWorld(snapshot, world)) {
    return false;
  }
  parent->scale = {4, -0.5F, 2};
  if (!snapshotMatchesWorld(snapshot, world)) {
    return false;
  }
  world.entities[0].id = "renamed";
  if (!snapshotMatchesWorld(snapshot, world) || snapshot.refresh(world)[1]) {
    return false;
  }
  world.entities[1].component<Transform3DComponent>()->parent = "renamed";
  if (!snapshotMatchesWorld(snapshot, world) || !snapshot.refresh(world)[1]) {
    return false;
  }
  std::swap(world.entities[0], world.entities[1]);
  if (!snapshotMatchesWorld(snapshot, world)) {
    return false;
  }
  world.entities[1].removeComponent<Transform3DComponent>();
  if (!snapshotMatchesWorld(snapshot, world) || snapshot.refresh(world)[0]) {
    return false;
  }
  world.entities[1].setComponent(Transform3DComponent{.parent = "child"});
  if (!snapshotMatchesWorld(snapshot, world) || snapshot.refresh(world)[0] ||
      snapshot.refresh(world)[1]) {
    return false;
  }
  world.entities[1].component<Transform3DComponent>()->parent.clear();
  if (!snapshotMatchesWorld(snapshot, world) || !snapshot.refresh(world)[0]) {
    return false;
  }
  // Replacement storage with identical entity count cannot preserve old IDs
  // or source transforms merely because the snapshot was already populated.
  World replacement;
  replacement.entities.push_back(transformed("other", {.position = {9, 8, 7}}));
  replacement.entities.push_back(transformed("leaf", {.parent = "other"}));
  if (!snapshotMatchesWorld(snapshot, replacement)) {
    return false;
  }
  replacement.entities.clear();
  return snapshotMatchesWorld(snapshot, replacement) &&
         snapshotMatchesWorld(snapshot, world);
}

} // namespace

int main() {
  using namespace demi::runtime;
  if (!checkTransformBasis() || !checkUnrotatedParentComposition() ||
      !checkRetainedSnapshot()) {
    std::cerr << "Shared transform basis or unrotated-parent composition "
                 "changed geometry.\n";
    return 1;
  }
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

  const auto bulk = resolveWorldTransforms3D(world);
  for (std::size_t index = 0; index < world.entities.size(); ++index) {
    const auto individual = resolveWorldTransform3D(world, world.entities[index]);
    if (bulk[index].has_value() != individual.has_value()) {
      std::cerr << "Bulk hierarchy resolution changed transform presence.\n";
      return 1;
    }
    if (!individual) {
      continue;
    }
    const auto direction = forwardDirection3D(*individual);
    const auto bulkDirection = forwardDirection3D(*bulk[index]);
    if (!close(bulk[index]->position.x, individual->position.x) ||
        !close(bulk[index]->position.y, individual->position.y) ||
        !close(bulk[index]->position.z, individual->position.z) ||
        !closeDirection(direction.x, bulkDirection.x) ||
        !closeDirection(direction.y, bulkDirection.y) ||
        !closeDirection(direction.z, bulkDirection.z)) {
      std::cerr << "Bulk hierarchy resolution changed composed geometry.\n";
      return 1;
    }
  }
  world.entities[0].component<Transform3DComponent>()->position.x += 4;
  const auto editedBulk = resolveWorldTransforms3D(world);
  if (!editedBulk[2] || !close(editedBulk[2]->position.x, bulk[2]->position.x + 4)) {
    std::cerr << "A new bulk hierarchy snapshot retained an old parent edit.\n";
    return 1;
  }
  world.entities[0].component<Transform3DComponent>()->parent = "grandchild";
  const auto cycleIssues = validateTransform3DHierarchy(world);
  if (cycleIssues.empty() ||
      cycleIssues.front().kind != Transform3DHierarchyIssueKind::Cycle ||
      resolveWorldTransform3D(world, world.entities[2]) ||
      resolveWorldTransforms3D(world)[2]) {
    std::cerr << "Transform3D hierarchy cycle was not detected safely.\n";
    return 1;
  }

  world.entities[0].component<Transform3DComponent>()->parent.clear();
  world.entities[1].component<Transform3DComponent>()->parent = "missing";
  const auto missingIssues = validateTransform3DHierarchy(world);
  if (missingIssues.empty() ||
      missingIssues.front().kind !=
          Transform3DHierarchyIssueKind::MissingParent ||
      resolveWorldTransforms3D(world)[1] ||
      resolveWorldTransforms3D(world)[2]) {
    std::cerr << "Missing Transform3D parent was not diagnosed.\n";
    return 1;
  }
  return 0;
}
