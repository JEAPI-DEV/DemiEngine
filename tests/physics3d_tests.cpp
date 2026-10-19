#include "demi/runtime/camera/Camera3DMath.h"
#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/physics/PhysicsBodyInputs3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/physics/SpatialQuery3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <iostream>
#include <utility>

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

bool near(const float left, const float right, const float epsilon = 0.05F) {
  return std::abs(left - right) <= epsilon;
}

Entity box(std::string id, const Vec3 position, const Vec3 size,
           const std::string &bodyType, const Vec3 velocity = {},
           const bool trigger = false, const bool gravity = false) {
  Entity entity;
  entity.id = std::move(id);
  entity.setComponent<Transform3DComponent>(
      Transform3DComponent{.position = position});
  entity.setComponent<BoxCollider3DComponent>(
      BoxCollider3DComponent{.size = size, .isTrigger = trigger});
  entity.setComponent<Rigidbody3DComponent>(
      Rigidbody3DComponent{.bodyType = bodyType,
                           .velocity = velocity,
                           .useGravity = gravity,
                           .continuous = true});
  return entity;
}

bool testNonphysicalBodyInputRejection() {
  Entity entity;
  entity.setComponent<Rigidbody3DComponent>(Rigidbody3DComponent{});
  entity.setComponent<BoxCollider3DComponent>(BoxCollider3DComponent{});
  const Entity &constantEntity = entity;
  const auto withoutTransform = physicsBodyInputs3D(entity);
  const auto constantWithoutTransform = physicsBodyInputs3D(constantEntity);
  if (withoutTransform.transform != nullptr || withoutTransform.hasCollider() ||
      withoutTransform.body != nullptr ||
      withoutTransform.mutableBody != nullptr ||
      constantWithoutTransform.hasCollider() ||
      constantWithoutTransform.body != nullptr) {
    std::cerr << "Non-3D entity unnecessarily resolved physics inputs\n";
    return false;
  }

  entity.setComponent<Transform3DComponent>(Transform3DComponent{});
  entity.removeComponent<BoxCollider3DComponent>();
  const auto withoutCollider = physicsBodyInputs3D(entity);
  const auto constantWithoutCollider = physicsBodyInputs3D(constantEntity);
  if (withoutCollider.transform == nullptr || withoutCollider.hasCollider() ||
      withoutCollider.body != nullptr ||
      withoutCollider.mutableBody != nullptr ||
      constantWithoutCollider.body != nullptr) {
    std::cerr
        << "Entity without collision unnecessarily resolved body inputs\n";
    return false;
  }
  return true;
}

bool testStaticHierarchyPoseSynchronization() {
  World world;
  Entity parent;
  parent.id = "parent";
  parent.setComponent<Transform3DComponent>(Transform3DComponent{});
  Entity child = box("child", {2, 0, 0}, {1, 1, 1}, "static");
  child.component<Transform3DComponent>()->parent = parent.id;
  world.entities.push_back(parent);
  world.entities.push_back(child);
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60.0F, {});

  const auto containsChild = [&](const Vec3 center) {
    const auto hits = physics.overlapSphere(center, 0.1F);
    return hits.size() == 1 && hits.front().entityId == "child";
  };
  if (!containsChild({2, 0, 0})) {
    std::cerr << "Initial static child world pose was incorrect\n";
    return false;
  }

  auto *parentTransform =
      findEntity(world, "parent")->component<Transform3DComponent>();
  parentTransform->position = {4, 0, 0};
  physics.step(world, 1.0F / 60.0F, {});
  if (!containsChild({6, 0, 0}) || containsChild({2, 0, 0})) {
    std::cerr << "Parent-only translation did not move static collision\n";
    return false;
  }

  parentTransform->rotation = {0, 1.57079632679F, 0};
  physics.step(world, 1.0F / 60.0F, {});
  if (!containsChild({4, 0, -2}) || containsChild({6, 0, 0})) {
    std::cerr << "Parent-only rotation did not move static collision\n";
    return false;
  }

  parentTransform->scale = {2, 2, 2};
  physics.step(world, 1.0F / 60.0F, {});
  if (!containsChild({4, 0, -4}) || containsChild({4, 0, -2})) {
    std::cerr << "Parent scaling did not rebuild static collision\n";
    return false;
  }

  Entity otherParent;
  otherParent.id = "other_parent";
  otherParent.setComponent<Transform3DComponent>(
      Transform3DComponent{.position = {10, 0, 0}});
  // Growth invalidates borrowed entity pointers; synchronization must resolve
  // the new addresses and parent identity on the next step.
  world.entities.push_back(otherParent);
  findEntity(world, "child")->component<Transform3DComponent>()->parent =
      "other_parent";
  physics.step(world, 1.0F / 60.0F, {});
  if (!containsChild({12, 0, 0}) || containsChild({4, 0, -4})) {
    std::cerr << "Reparenting did not move static collision\n";
    return false;
  }
  return true;
}

bool testParentedDynamicPoseOwnership() {
  World world;
  Entity parent;
  parent.id = "parent";
  parent.setComponent<Transform3DComponent>(Transform3DComponent{});
  Entity child = box("child", {2, 0, 0}, {1, 1, 1}, "dynamic", {3, 0, 0});
  child.component<Transform3DComponent>()->parent = parent.id;
  child.component<Rigidbody3DComponent>()->linearDamping = 0.0F;
  child.component<Rigidbody3DComponent>()->allowSleep = false;
  world.entities.push_back(parent);
  world.entities.push_back(child);
  auto &physics = ensurePhysicsWorld3D(world);
  constexpr float fixedDt = 1.0F / 60.0F;
  constexpr float displacementPerStep = 3.0F * fixedDt;

  const auto hasNativePosition = [&](const float expectedX) {
    const auto snapshot = physics.motionSnapshot("child");
    return snapshot && near(snapshot->center.x, expectedX, 0.002F) &&
           near(snapshot->center.y, 0.0F, 0.002F) &&
           near(snapshot->center.z, 0.0F, 0.002F);
  };
  for (int step = 1; step <= 4; ++step) {
    physics.step(world, fixedDt, {});
    if (!hasNativePosition(2.0F + step * displacementPerStep)) {
      std::cerr << "Unchanged parented dynamic body replayed authored pose\n";
      return false;
    }
  }

  // A changed authored hierarchy deliberately resets the native pose before
  // simulation; unchanged following steps must retain native motion ownership.
  findEntity(world, "parent")->component<Transform3DComponent>()->position = {
      4, 0, 0};
  for (int step = 1; step <= 4; ++step) {
    physics.step(world, fixedDt, {});
    if (!hasNativePosition(6.0F + step * displacementPerStep)) {
      std::cerr << "Parent edit or subsequent native motion was lost\n";
      return false;
    }
  }
  const auto *authoredTransform =
      findEntity(world, "child")->component<Transform3DComponent>();
  if (!near(authoredTransform->position.x, 2.0F, 0.002F)) {
    std::cerr << "Parented dynamic simulation changed authored local pose\n";
    return false;
  }
  return true;
}

bool testNativeColliderOffsetSynchronization() {
  const auto testOffset = []<typename Collider>(Collider collider) {
    World world;
    Entity entity;
    entity.id = "collider";
    entity.setComponent<Transform3DComponent>(Transform3DComponent{});
    entity.setComponent<Collider>(std::move(collider));
    world.entities.push_back(entity);
    auto &physics = ensurePhysicsWorld3D(world);
    physics.step(world, 1.0F / 60.0F, {});
    if (physics.overlapSphere({}, 0.1F).size() != 1) {
      std::cerr << "Initial collider offset probe was unavailable\n";
      return false;
    }
    world.entities.front().component<Collider>()->offset = {5, 0, 0};
    physics.step(world, 1.0F / 60.0F, {});
    const auto movedHits = physics.overlapSphere({5, 0, 0}, 0.1F);
    if (!physics.overlapSphere({}, 0.1F).empty() || movedHits.size() != 1 ||
        movedHits.front().entityId != "collider") {
      std::cerr << "Native collider offset edit did not rebuild collision\n";
      return false;
    }
    // An unchanged step must preserve the resulting shape and query identity.
    physics.step(world, 1.0F / 60.0F, {});
    return physics.overlapSphere({5, 0, 0}, 0.1F).size() == 1;
  };

  ConvexCollider3DComponent convex;
  convex.points = {{-0.5F, -0.5F, -0.5F}, {0.5F, -0.5F, -0.5F},
                   {-0.5F, 0.5F, -0.5F},  {0.5F, 0.5F, -0.5F},
                   {-0.5F, -0.5F, 0.5F},  {0.5F, -0.5F, 0.5F},
                   {-0.5F, 0.5F, 0.5F},   {0.5F, 0.5F, 0.5F}};
  return testOffset(BoxCollider3DComponent{}) &&
         testOffset(SphereCollider3DComponent{}) &&
         testOffset(CapsuleCollider3DComponent{}) && testOffset(convex);
}

bool testSleepingAndWakeCommands() {
  World world;
  world.entities.push_back(box("floor", {0, -0.5F, 0}, {20, 1, 20}, "static"));
  world.entities.push_back(
      box("sleeper", {0, 0.5F, 0}, {1, 1, 1}, "dynamic", {}, false, true));
  world.entities.push_back(
      box("always_awake", {3, 0.5F, 0}, {1, 1, 1}, "dynamic", {}, false, true));
  world.entities.back().component<Rigidbody3DComponent>()->allowSleep = false;
  for (int step = 0; step < 240; ++step)
    stepPhysics3D(world, 1.0F / 60.0F);
  auto *sleeper =
      findEntity(world, "sleeper")->component<Rigidbody3DComponent>();
  if (sleeper->awake || !findEntity(world, "always_awake")
                             ->component<Rigidbody3DComponent>()
                             ->awake) {
    std::cerr << "Natural sleeping or allow_sleep=false failed\n";
    return false;
  }
  if (!setRigidbodyVelocity3D(world, "sleeper", {2, 0, 0}))
    return false;
  stepPhysics3D(world, 1.0F / 60.0F);
  if (!sleeper->awake || sleeper->velocity.x < 1.0F) {
    std::cerr << "Velocity command was overwritten by stale sleeping state\n";
    return false;
  }
  if (!setRigidbodyAwake3D(world, "sleeper", false))
    return false;
  stepPhysics3D(world, 1.0F / 60.0F);
  if (sleeper->awake)
    return false;
  if (!addRigidbodyImpulse3D(world, "sleeper", {0, 3, 0}))
    return false;
  stepPhysics3D(world, 1.0F / 60.0F);
  if (!sleeper->awake || sleeper->velocity.y < 2.0F)
    return false;
  // Authored component edits remain supported, independently of Lua helpers.
  sleeper->awake = false;
  stepPhysics3D(world, 1.0F / 60.0F);
  if (sleeper->awake)
    return false;
  sleeper->awake = true;
  sleeper->velocity = {0, 4, 0};
  stepPhysics3D(world, 1.0F / 60.0F);
  if (!sleeper->awake || sleeper->velocity.y < 3.0F)
    return false;
  return true;
}

bool testSleepingSupportChanges() {
  for (int change = 0; change < 4; ++change) {
    World world;
    world.entities.push_back(
        box("support", {0, -0.5F, 0}, {4, 1, 4}, "static"));
    for (int i = 0; i < 3; ++i)
      world.entities.push_back(box("stack_" + std::to_string(i),
                                   {0, 0.5F + i, 0}, {1, 1, 1}, "dynamic", {},
                                   false, true));
    for (int step = 0; step < 360; ++step)
      stepPhysics3D(world, 1.0F / 60.0F);
    for (int i = 0; i < 3; ++i) {
      const auto *body = findEntity(world, "stack_" + std::to_string(i))
                             ->component<Rigidbody3DComponent>();
      if (body->awake) {
        std::cerr << "Resting stack did not sleep\n";
        return false;
      }
    }
    if (change == 0)
      world.entities.erase(world.entities.begin());
    if (change == 1 && !setRigidbodyEnabled3D(world, "support", false))
      return false;
    if (change == 2)
      findEntity(world, "support")
          ->component<Transform3DComponent>()
          ->position.x = 20;
    if (change == 3)
      findEntity(world, "support")
          ->component<BoxCollider3DComponent>()
          ->offset.x = 20;
    for (int step = 0; step < 120; ++step)
      stepPhysics3D(world, 1.0F / 60.0F);
    for (int i = 0; i < 3; ++i) {
      const auto *entity = findEntity(world, "stack_" + std::to_string(i));
      if (entity->component<Transform3DComponent>()->position.y > -3) {
        std::cerr << "Sleeping body floated after support change " << change
                  << '\n';
        return false;
      }
    }
    for (const auto &contact : world.physicsContacts3D)
      if (contact.entityId == "support" || contact.otherEntityId == "support") {
        std::cerr << "Stale sleeping support contact\n";
        return false;
      }
  }
  return true;
}

bool testSleepingEditsAndReusedBodySlots() {
  World world;
  world.entities.push_back(box("floor", {0, -0.5F, 0}, {20, 1, 20}, "static"));
  world.entities.push_back(
      box("crate", {0, 0.5F, 0}, {1, 1, 1}, "dynamic", {}, false, true));
  for (int step = 0; step < 240; ++step)
    stepPhysics3D(world, 1.0F / 60.0F);
  if (!setRigidbodyReportContacts3D(world, "crate", false) ||
      !setRigidbodyReportContacts3D(world, "floor", false))
    return false;
  for (int step = 0; step < 120; ++step)
    stepPhysics3D(world, 1.0F / 60.0F);
  if (!world.physicsContacts3D.empty())
    return false;
  if (!setRigidbodyReportContacts3D(world, "crate", true))
    return false;
  stepPhysics3D(world, 1.0F / 60.0F);
  if (contactsForEntity3D(world, "crate").empty())
    return false;
  for (int step = 0; step < 120; ++step)
    stepPhysics3D(world, 1.0F / 60.0F);
  findEntity(world, "crate")->component<BoxCollider3DComponent>()->size.y =
      2.0F;
  for (int step = 0; step < 120; ++step)
    stepPhysics3D(world, 1.0F / 60.0F);
  if (findEntity(world, "crate")
          ->component<Transform3DComponent>()
          ->position.y < 0.9F)
    return false;
  // Remove a sleeping body, then force native slot reuse on a following step.
  world.entities.erase(world.entities.begin() + 1);
  stepPhysics3D(world, 1.0F / 60.0F);
  world.entities.push_back(
      box("replacement", {7, 4, 0}, {1, 1, 1}, "dynamic", {}, false, false));
  for (int step = 0; step < 120; ++step)
    stepPhysics3D(world, 1.0F / 60.0F);
  auto *body =
      findEntity(world, "replacement")->component<Rigidbody3DComponent>();
  if (body->awake)
    return false;
  body->useGravity = true;
  for (int step = 0; step < 120; ++step)
    stepPhysics3D(world, 1.0F / 60.0F);
  if (!near(findEntity(world, "replacement")
                ->component<Transform3DComponent>()
                ->position.y,
            0.5F, 0.08F))
    return false;
  for (const auto &contact : world.physicsContacts3D)
    if (contact.entityId == "crate" || contact.otherEntityId == "crate")
      return false;
  return true;
}

bool testContactIdentityAndEntityLifetime() {
  World world;
  const std::string prefix(48, 'a');
  const std::string triggerId = prefix + "_trigger";
  world.entities.push_back(
      box(triggerId, {0, 0, 0}, {20, 2, 2}, "static", {}, true));
  // Native allocation order deliberately differs from authored-ID order.
  const std::vector<std::string> ids{prefix + "9", prefix + "10", prefix + "1"};
  for (std::size_t i = 0; i < ids.size(); ++i) {
    world.entities.push_back(box(ids[i], {float(i) * 3, 0, 0}, {1, 1, 1},
                                 "dynamic"));
    world.entities.back().component<Rigidbody3DComponent>()->allowSleep = false;
  }
  const auto canonicalPair = [](const PhysicsContact3D &contact) {
    return std::pair{std::min(contact.entityId, contact.otherEntityId),
                     std::max(contact.entityId, contact.otherEntityId)};
  };
  for (int step = 0; step < 2; ++step) {
    stepPhysics3D(world, 1.0F / 60.0F);
    if (world.physicsContacts3D.size() != 6 ||
        !std::ranges::is_sorted(world.physicsContacts3D, {}, canonicalPair))
      return false;
    for (const auto &id : ids) {
      const auto contacts = contactsForEntity3D(world, id);
      if (contacts.size() != 1 || contacts[0].otherEntityId != triggerId ||
          contacts[0].phase != (step == 0 ? "enter" : "stay") ||
          !contacts[0].isTrigger)
        return false;
    }
  }
  // Entity disabling removes its native body; enabling recreates it with a new
  // generation. Old contacts must exit once, then the recreated pair enters.
  findEntity(world, ids[0])->enabled = false;
  stepPhysics3D(world, 1.0F / 60.0F);
  auto contacts = contactsForEntity3D(world, ids[0]);
  if (contacts.size() != 1 || contacts[0].phase != "exit")
    return false;
  stepPhysics3D(world, 1.0F / 60.0F);
  if (!contactsForEntity3D(world, ids[0]).empty())
    return false;
  findEntity(world, ids[0])->enabled = true;
  stepPhysics3D(world, 1.0F / 60.0F);
  contacts = contactsForEntity3D(world, ids[0]);
  if (contacts.size() != 1 || contacts[0].phase != "enter")
    return false;
  // Losing the collider must also retire the native body and its contacts.
  findEntity(world, ids[0])->removeComponent<BoxCollider3DComponent>();
  stepPhysics3D(world, 1.0F / 60.0F);
  contacts = contactsForEntity3D(world, ids[0]);
  if (contacts.size() != 1 || contacts[0].phase != "exit")
    return false;
  stepPhysics3D(world, 1.0F / 60.0F);
  return contactsForEntity3D(world, ids[0]).empty();
}

bool testFixedStepSimulation() {
  World world;
  world.entities.push_back(
      box("floor", {0.0F, -0.5F, 0.0F}, {20.0F, 1.0F, 20.0F}, "static"));
  world.entities.push_back(box("crate", {0.0F, 4.0F, 0.0F}, {1.0F, 1.0F, 1.0F},
                               "dynamic", {}, false, true));
  for (int step = 0; step < 300; ++step)
    stepPhysics3D(world, 1.0F / 60.0F);
  const Entity *crate = findEntity(world, "crate");
  if (crate == nullptr ||
      !near(crate->component<Transform3DComponent>()->position.y, 0.5F,
            0.08F)) {
    std::cerr << "A gravity-driven body did not settle on the floor.\n";
    return false;
  }
  const auto contacts = contactsForEntity3D(world, "crate");
  if (!std::ranges::any_of(contacts, [](const PhysicsContact3D &contact) {
        return contact.otherEntityId == "floor" && contact.phase != "exit" &&
               !contact.isTrigger;
      })) {
    std::cerr << "Settled body did not expose its floor contact.\n";
    return false;
  }
  if (!setRigidbodyReportContacts3D(world, "crate", false) ||
      !setRigidbodyReportContacts3D(world, "floor", false)) {
    std::cerr << "Could not disable 3D contact reporting.\n";
    return false;
  }
  stepPhysics3D(world, 1.0F / 60.0F);
  stepPhysics3D(world, 1.0F / 60.0F);
  if (!world.physicsContacts3D.empty()) {
    std::cerr << "Opted-out 3D bodies retained reported contacts.\n";
    return false;
  }
  return true;
}

bool testCollisionFromEverySide() {
  const Vec3 starts[]{{-2.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
                      {0.0F, -2.0F, 0.0F}, {0.0F, 2.0F, 0.0F},
                      {0.0F, 0.0F, -2.0F}, {0.0F, 0.0F, 2.0F}};
  for (int index = 0; index < 6; ++index) {
    World world;
    const Vec3 start = starts[index];
    const Vec3 velocity{-start.x * 8.0F, -start.y * 8.0F, -start.z * 8.0F};
    world.entities.push_back(
        box("blocked_cell", {}, {1.0F, 1.0F, 1.0F}, "static"));
    world.entities.push_back(
        box("player", start, {0.5F, 0.5F, 0.5F}, "dynamic", velocity));
    for (int step = 0; step < 60; ++step)
      stepPhysics3D(world, 1.0F / 120.0F, {});
    const Vec3 position = findEntity(world, "player")
                              ->component<Transform3DComponent>()
                              ->position;
    if (std::abs(position.x) < 0.72F && std::abs(position.y) < 0.72F &&
        std::abs(position.z) < 0.72F) {
      std::cerr << "Collider admitted the player from side " << index << ".\n";
      return false;
    }
  }
  return true;
}

bool testContinuousCollisionAndCommands() {
  World world;
  world.entities.push_back(box("thin_wall", {}, {0.05F, 4.0F, 4.0F}, "static"));
  world.entities.push_back(box("projectile", {-10.0F, 0.0F, 0.0F},
                               {0.2F, 0.2F, 0.2F}, "dynamic",
                               {200.0F, 0.0F, 0.0F}));
  for (int step = 0; step < 12; ++step)
    stepPhysics3D(world, 1.0F / 60.0F, {});
  const Entity *projectile = findEntity(world, "projectile");
  if (projectile->component<Transform3DComponent>()->position.x > 0.2F) {
    std::cerr << "Continuous 3D body tunneled through a thin wall.\n";
    return false;
  }
  if (!setRigidbodyContinuous3D(world, "projectile", false) ||
      projectile->component<Rigidbody3DComponent>()->continuous ||
      !setRigidbodyContinuous3D(world, "projectile", true) ||
      !projectile->component<Rigidbody3DComponent>()->continuous) {
    std::cerr << "Runtime 3D continuous collision switching failed.\n";
    return false;
  }

  World impulseWorld;
  Entity light =
      box("light", {-2.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F}, "dynamic");
  Entity heavy =
      box("heavy", {2.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F}, "dynamic");
  light.component<Rigidbody3DComponent>()->mass = 1.0F;
  heavy.component<Rigidbody3DComponent>()->mass = 4.0F;
  impulseWorld.entities.push_back(std::move(light));
  impulseWorld.entities.push_back(std::move(heavy));
  if (!addRigidbodyImpulse3D(impulseWorld, "light", {4.0F, 0.0F, 0.0F}) ||
      !addRigidbodyImpulse3D(impulseWorld, "heavy", {4.0F, 0.0F, 0.0F}))
    return false;
  stepPhysics3D(impulseWorld, 1.0F / 60.0F, {});
  const float lightSpeed = rigidbodyVelocity3D(impulseWorld, "light")->x;
  const float heavySpeed = rigidbodyVelocity3D(impulseWorld, "heavy")->x;
  if (!(lightSpeed > heavySpeed * 3.5F)) {
    std::cerr << "Mass did not affect 3D impulse response.\n";
    return false;
  }
  if (!setRigidbodyEnabled3D(impulseWorld, "heavy", false) ||
      !setRigidbodyEnabled3D(impulseWorld, "heavy", true)) {
    std::cerr << "Rigidbody enable/disable command failed.\n";
    return false;
  }
  return true;
}

bool testTriggersAndFiltering() {
  World world;
  Entity trigger = box("pickup", {}, {2.0F, 2.0F, 2.0F}, "static", {}, true);
  trigger.component<BoxCollider3DComponent>()->layer = "pickup";
  Entity player = box("player", {-3.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                      "dynamic", {5.0F, 0.0F, 0.0F});
  player.component<BoxCollider3DComponent>()->layer = "player";
  world.entities.push_back(std::move(trigger));
  world.entities.push_back(std::move(player));
  bool entered = false;
  bool stayed = false;
  bool exited = false;
  for (int step = 0; step < 60; ++step) {
    stepPhysics3D(world, 1.0F / 60.0F, {});
    for (const PhysicsContact3D &contact :
         contactsForEntity3D(world, "player")) {
      entered |= contact.phase == "enter" && contact.isTrigger;
      stayed |= contact.phase == "stay" && contact.isTrigger;
      exited |= contact.phase == "exit" && contact.isTrigger;
    }
  }
  if (!setRigidbodyVelocity3D(world, "player", {8.0F, 0.0F, 0.0F}))
    return false;
  for (int step = 0; step < 90; ++step) {
    stepPhysics3D(world, 1.0F / 60.0F, {});
    exited |= std::ranges::any_of(contactsForEntity3D(world, "player"),
                                  [](const PhysicsContact3D &contact) {
                                    return contact.phase == "exit" &&
                                           contact.isTrigger;
                                  });
  }
  if (!entered || !stayed || !exited) {
    std::cerr << "Trigger enter/stay/exit lifecycle was incomplete: enter="
              << entered << ", stay=" << stayed << ", exit=" << exited << ".\n";
    return false;
  }

  World filtered;
  filtered.physicsCategoryBits = {{"player", 1}, {"wall", 2}};
  filtered.physicsMaskBits = {{"player", 1}, {"wall", 2}};
  Entity wall = box("wall", {}, {1.0F, 4.0F, 4.0F}, "static");
  wall.component<BoxCollider3DComponent>()->layer = "wall";
  Entity ghost = box("ghost", {-2.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                     "dynamic", {5.0F, 0.0F, 0.0F});
  ghost.component<BoxCollider3DComponent>()->layer = "player";
  filtered.entities.push_back(std::move(wall));
  filtered.entities.push_back(std::move(ghost));
  for (int step = 0; step < 90; ++step)
    stepPhysics3D(filtered, 1.0F / 60.0F, {});
  if (findEntity(filtered, "ghost")
          ->component<Transform3DComponent>()
          ->position.x < 0.6F) {
    std::cerr << "Collision layer masks did not reject a body pair.\n";
    return false;
  }
  return true;
}

bool testCharacterAndCameraMath() {
  World world;
  world.entities.push_back(
      box("floor", {0.0F, -0.5F, 0.0F}, {12.0F, 1.0F, 12.0F}, "static"));
  world.entities.push_back(
      box("wall", {1.5F, 1.0F, 0.0F}, {1.0F, 2.0F, 4.0F}, "static"));
  Entity player;
  player.id = "character";
  player.setComponent<Transform3DComponent>(
      Transform3DComponent{.position = {0.0F, 1.0F, 0.0F}});
  player.setComponent<CharacterController3DComponent>(
      CharacterController3DComponent{});
  player.setComponent<CapsuleCollider3DComponent>(
      CapsuleCollider3DComponent{.radius = 0.4F, .height = 1.8F});
  world.entities.push_back(std::move(player));
  for (int step = 0; step < 120; ++step) {
    if (!setCharacterVelocity3D(world, "character", {4.0F, 0.0F, 0.0F}))
      return false;
    stepPhysics3D(world, 1.0F / 60.0F);
  }
  const auto state = characterState3D(world, "character");
  const Vec3 position = findEntity(world, "character")
                            ->component<Transform3DComponent>()
                            ->position;
  if (!state || !state->grounded || position.x > 0.7F) {
    std::cerr << "Capsule controller grounding or wall slide failed.\n";
    return false;
  }
  if (!requestCharacterJump3D(world, "character", 6.0F))
    return false;
  stepPhysics3D(world, 1.0F / 60.0F);
  if (characterState3D(world, "character")->appliedMotion.y <= 0.0F) {
    std::cerr << "Grounded character jump was not applied.\n";
    return false;
  }

  Camera3DComponent camera;
  WorldTransform3D transform;
  const CameraRay3D center =
      cameraScreenRay3D(transform, camera, {400.0F, 300.0F}, {800.0F, 600.0F});
  const auto projected =
      worldToScreen3D(transform, camera, {0.0F, 0.0F, 10.0F}, {800.0F, 600.0F});
  if (!near(center.direction.z, 1.0F, 0.001F) || !projected ||
      !near(projected->x, 400.0F) || !near(projected->y, 300.0F) ||
      worldToScreen3D(transform, camera, {0.0F, 0.0F, -1.0F},
                      {800.0F, 600.0F})) {
    std::cerr << "3D camera screen/world conversion failed.\n";
    return false;
  }
  // Off-centre rays must agree with the renderer's right-handed look-at.
  const auto defaultRight =
      cameraScreenRay3D(transform, camera, {600, 300}, {800, 600});
  if (defaultRight.direction.x >= 0) {
    std::cerr << "Camera ray handedness differs from rendering.\n";
    return false;
  }
  transform.position = {0, 3, 10};
  camera.targetOffset = {0, -1, -10};
  const auto aimed =
      cameraScreenRay3D(transform, camera, {400, 300}, {800, 600});
  const auto aimedScreen =
      worldToScreen3D(transform, camera, {0, 2, 0}, {800, 600});
  if (!near(aimed.direction.z, -10.0F / std::sqrt(101.0F), 0.001F) ||
      !near(aimed.direction.y, -1.0F / std::sqrt(101.0F), 0.001F) ||
      !aimedScreen || !near(aimedScreen->x, 400) ||
      !near(aimedScreen->y, 300)) {
    std::cerr << "Camera conversions ignored the authored target offset.\n";
    return false;
  }
  camera.targetOffset = {0, 0, -1};
  camera.perspective = false;
  camera.orthographicSize = 8;
  const auto corner =
      cameraScreenRay3D(transform, camera, {800, 0}, {800, 600});
  const auto cornerScreen =
      worldToScreen3D(transform, camera, {16.0F / 3.0F, 7, 0}, {800, 600});
  if (!near(corner.origin.x, 16.0F / 3.0F) ||
      !near(corner.origin.y, 7) || !cornerScreen ||
      !near(cornerScreen->x, 800) || !near(cornerScreen->y, 0)) {
    std::cerr << "Orthographic picking differs from the rendered extent.\n";
    return false;
  }
  camera.upAxis = -1;
  const auto inverted =
      cameraScreenRay3D(transform, camera, {800, 0}, {800, 600});
  if (!near(inverted.origin.x, -16.0F / 3.0F) ||
      !near(inverted.origin.y, -1)) {
    std::cerr << "Camera picking ignored the authored up axis.\n";
    return false;
  }
  const Vec3 rotation = lookAtRotation3D({}, {1.0F, 0.0F, 0.0F});
  const Vec3 forward =
      forwardDirection3D(WorldTransform3D{.rotation = rotation});
  if (!near(forward.x, 1.0F, 0.001F)) {
    std::cerr << "Transform3D look-at/direction helpers disagree.\n";
    return false;
  }
  return true;
}

bool testOffsetCharacterRetainsSupportAfterMoving() {
  for (const bool capsule : {true, false}) {
    for (const float centerHeight : {1.0F, 1.125F}) {
      for (const bool prepared : {false, true}) {
        World world;
        Entity origin;
        origin.id = "terrain_origin";
        origin.setComponent(Transform3DComponent{.position = {-360, 0, -200}});
        world.entities.push_back(std::move(origin));
        auto geometry = std::make_shared<ColliderAsset3D>();
        geometry->revision = 1;
        for (int z = 192; z < 256; z += 2) {
          for (int x = 320; x < 384; x += 2) {
            const Vec3 a{float(x), 0, float(z)};
            const Vec3 b{float(x), 0, float(z + 2)};
            const Vec3 c{float(x + 2), 0, float(z + 2)};
            const Vec3 d{float(x + 2), 0, float(z)};
            geometry->triangles.push_back({a, b, c});
            geometry->triangles.push_back({a, c, d});
          }
        }
        Entity floor;
        floor.id = "grid_floor";
        floor.setComponent(Transform3DComponent{.parent = "terrain_origin"});
        ModelCollider3DComponent terrainCollider;
        terrainCollider.inlineGeometry = std::move(geometry);
        floor.setComponent(std::move(terrainCollider));
        world.entities.push_back(std::move(floor));
        Entity rider;
        rider.id = "foot_origin_character";
        rider.setComponent(Transform3DComponent{.position = {0, 0.8F, 0},
                                                .rotation = {0, -0.021F, 0}});
        rider.setComponent(CharacterController3DComponent{
            .stepHeight = 0.45F, .slopeLimit = 48, .gravity = -22});
        if (capsule) {
          rider.setComponent(CapsuleCollider3DComponent{
              .radius = 0.6F, .height = 2, .offset = {0, centerHeight, 0}});
        } else {
          rider.setComponent(BoxCollider3DComponent{
              .size = {1.2F, 2, 1.2F}, .offset = {0, centerHeight, 0}});
        }
        world.entities.push_back(std::move(rider));
        if (prepared) {
          std::string error;
          if (!preparePhysicsShapes3D(world, error)) {
            std::cerr << "Foot-origin fixture shape preparation failed: "
                      << error << '\n';
            return false;
          }
        }
        for (int frame = 0; frame < 60; ++frame) {
          stepPhysics3D(world, 1.0F / 60.0F, {});
        }
        // Walking over translated terrain triangles can put a contact a few
        // floating-point units above visual-origin Y=0. Such contacts still lie
        // below the offset collider's center and must remain valid support.
        for (int segment = 0; segment < 8; ++segment) {
          float speed = 0.0F;
          for (int frame = 0; frame < 60; ++frame) {
            speed = 3.2F * float(frame + 1) / 60.0F;
            if (!setCharacterVelocity3D(world, "foot_origin_character",
                                        {-0.021F * speed, 0, speed})) {
              return false;
            }
            stepPhysics3D(world, 1.0F / 60.0F, {});
          }
          for (int frame = 0; frame < 60; ++frame) {
            speed = std::max(0.0F, speed - 40.0F / 60.0F);
            if (!setCharacterVelocity3D(world, "foot_origin_character",
                                        {-0.021F * speed, 0, speed})) {
              return false;
            }
            stepPhysics3D(world, 1.0F / 60.0F, {});
          }
          const auto *character = findEntity(world, "foot_origin_character");
          const auto *controller =
              character->component<CharacterController3DComponent>();
          const auto position =
              character->component<Transform3DComponent>()->position;
          if (!controller->grounded ||
              controller->groundEntity != "grid_floor" ||
              !near(position.y, 1.0F - centerHeight, 0.03F)) {
            std::cerr << "Offset " << (capsule ? "capsule" : "box")
                      << " lost support after movement segment " << segment
                      << " (prepared=" << prepared << ", Y=" << position.y
                      << ").\n";
            return false;
          }
        }
      }
    }
  }
  return true;
}

bool testCharacterUsesSelectedBoxCollider() {
  const Vec3 starts[]{{-3.0F, 0.0F, 0.0F},
                      {3.0F, 0.0F, 0.0F},
                      {0.0F, 0.0F, -3.0F},
                      {0.0F, 0.0F, 3.0F}};
  for (int side = 0; side < 4; ++side) {
    World world;
    world.entities.push_back(box("wall", {}, {1.0F, 2.0F, 1.0F}, "static"));
    Entity character;
    character.id = "box_character";
    character.setComponent<Transform3DComponent>(
        Transform3DComponent{.position = starts[side]});
    character.setComponent<CharacterController3DComponent>(
        CharacterController3DComponent{.gravity = 0.0F});
    character.setComponent<BoxCollider3DComponent>(
        BoxCollider3DComponent{.size = {1.6F, 1.0F, 0.4F}});
    world.entities.push_back(std::move(character));

    const Vec3 velocity{-starts[side].x * 2.0F, 0.0F, -starts[side].z * 2.0F};
    for (int step = 0; step < 90; ++step) {
      if (!setCharacterVelocity3D(world, "box_character", velocity))
        return false;
      stepPhysics3D(world, 1.0F / 60.0F, {});
    }
    const Vec3 position = findEntity(world, "box_character")
                              ->component<Transform3DComponent>()
                              ->position;
    const bool isXAxis = starts[side].x != 0.0F;
    const float axisPosition = isXAxis ? position.x : position.z;
    const float startAxis = isXAxis ? starts[side].x : starts[side].z;
    const float minimumDistance = isXAxis ? 1.2F : 0.62F;
    if (axisPosition * startAxis <= 0.0F ||
        std::abs(axisPosition) < minimumDistance) {
      std::cerr << "Box character collider failed from side " << side
                << "; stopped at " << axisPosition << ".\n";
      return false;
    }
  }

  World resized;
  resized.entities.push_back(box("wall", {}, {1.0F, 2.0F, 1.0F}, "static"));
  Entity character;
  character.id = "resized_character";
  character.setComponent<Transform3DComponent>(
      Transform3DComponent{.position = {-3.0F, 0.0F, 0.0F}});
  character.setComponent<CharacterController3DComponent>(
      CharacterController3DComponent{.gravity = 0.0F});
  character.setComponent<BoxCollider3DComponent>(
      BoxCollider3DComponent{.size = {1.6F, 1.0F, 0.4F}});
  resized.entities.push_back(std::move(character));
  stepPhysics3D(resized, 1.0F / 60.0F, {});
  findEntity(resized, "resized_character")
      ->component<BoxCollider3DComponent>()
      ->size.x = 0.4F;
  for (int step = 0; step < 90; ++step) {
    if (!setCharacterVelocity3D(resized, "resized_character",
                                {6.0F, 0.0F, 0.0F}))
      return false;
    stepPhysics3D(resized, 1.0F / 60.0F, {});
  }
  const float resizedX = findEntity(resized, "resized_character")
                             ->component<Transform3DComponent>()
                             ->position.x;
  if (resizedX >= 0.0F || std::abs(resizedX) > 0.9F) {
    std::cerr
        << "Runtime character collider resize was not applied; stopped at "
        << resizedX << ".\n";
    return false;
  }
  return true;
}

bool testAirborneJumpRequestIsNotBufferedByPhysics() {
  World world;
  Entity player;
  player.id = "airborne_character";
  player.setComponent<Transform3DComponent>(
      Transform3DComponent{.position = {0.0F, 4.0F, 0.0F}});
  player.setComponent<CharacterController3DComponent>(
      CharacterController3DComponent{});
  player.setComponent<CapsuleCollider3DComponent>(
      CapsuleCollider3DComponent{.radius = 0.4F, .height = 1.8F});
  world.entities.push_back(std::move(player));

  if (!requestCharacterJump3D(world, "airborne_character", 6.0F))
    return false;
  stepPhysics3D(world, 1.0F / 60.0F);

  const auto state = characterState3D(world, "airborne_character");
  const auto *controller = findEntity(world, "airborne_character")
                               ->component<CharacterController3DComponent>();
  if (!state || state->appliedMotion.y >= 0.0F ||
      controller->requestedJumpSpeed != 0.0F) {
    std::cerr << "Physics retained or applied an airborne jump request; "
                 "jump buffering belongs in gameplay code.\n";
    return false;
  }
  return true;
}

bool testShapeQueriesAndColliderKinds() {
  World world;
  Entity capsule;
  capsule.id = "capsule";
  capsule.setComponent<Transform3DComponent>(
      Transform3DComponent{.position = {2.0F, 0.0F, 0.0F}});
  capsule.setComponent<CapsuleCollider3DComponent>(CapsuleCollider3DComponent{
      .radius = 0.4F, .height = 2.0F, .layer = "actors"});
  Entity convex;
  convex.id = "convex";
  convex.setComponent<Transform3DComponent>(
      Transform3DComponent{.position = {-2.0F, 0.0F, 0.0F}});
  convex.setComponent<ConvexCollider3DComponent>(
      ConvexCollider3DComponent{.points = {{-0.5F, -0.5F, -0.5F},
                                           {0.5F, -0.5F, -0.5F},
                                           {0.0F, 0.5F, -0.5F},
                                           {0.0F, 0.0F, 0.5F}},
                                .layer = "geometry"});
  world.entities.push_back(std::move(capsule));
  world.entities.push_back(std::move(convex));
  world.entities.push_back(
      box("thin", {0.0F, 0.0F, 0.0F}, {0.02F, 2.0F, 2.0F}, "static"));
  stepPhysics3D(world, 1.0F / 60.0F, {});

  const auto actors =
      overlapSphereAll3D(world, {2.0F, 0.0F, 0.0F}, 0.2F, "actors");
  const auto geometry = overlapBoxAll3D(world, {-2.0F, 0.0F, 0.0F},
                                        {1.5F, 1.5F, 1.5F}, "geometry");
  const auto capsuleOverlap =
      overlapCapsuleAll3D(world, {2.0F, 0.0F, 0.0F}, 0.5F, 2.0F);
  const auto sphereSweep = sphereCast3D(world, {-3.0F, 0.0F, 0.0F}, 0.1F,
                                        {1.0F, 0.0F, 0.0F}, 6.0F, {}, "convex");
  const auto capsuleSweep =
      capsuleCast3D(world, {-3.0F, 0.0F, 0.0F}, 0.2F, 1.0F, {1.0F, 0.0F, 0.0F},
                    6.0F, {}, "convex");
  if (actors.size() != 1 || actors.front().entityId != "capsule" ||
      actors.front().layer != "actors" || geometry.size() != 1 ||
      geometry.front().entityId != "convex" || capsuleOverlap.empty() ||
      !sphereSweep || sphereSweep->entityId != "thin" || !capsuleSweep ||
      capsuleSweep->entityId != "thin") {
    std::cerr << "Rich overlap or shape-cast query failed.\n";
    return false;
  }
  if (sphereCast3D(world, {}, -1.0F, {1, 0, 0}, 1.0F) ||
      sphereCast3D(world, {}, 0.1F, {}, 1.0F) ||
      capsuleCast3D(world, {}, 1.0F, 1.0F, {1, 0, 0}, 1.0F)) {
    std::cerr << "Invalid 3D shape query input was accepted.\n";
    return false;
  }
  world.entities[0].component<CapsuleCollider3DComponent>()->height = 0.8F;
  stepPhysics3D(world, 1.0F / 60.0F, {});
  const auto roundCapsule =
      overlapCapsuleAll3D(world, {2, 0, 0}, 0.4F, 0.8F, "actors");
  const auto roundSweep =
      capsuleCast3D(world, {-1, 0, 0}, 0.2F, 0.4F, {1, 0, 0}, 4.0F);
  if (roundCapsule.size() != 1 || !roundSweep) {
    std::cerr << "Sphere-shaped capsule query failed.\n";
    return false;
  }
  return true;
}

bool testSolidOnlySphereCast() {
  World world;
  auto trigger = makeStaticBox(); trigger.id = "sensor";
  trigger.component<Transform3DComponent>()->position.x = 1;
  trigger.component<BoxCollider3DComponent>()->isTrigger = true;
  auto wall = makeStaticBox(); wall.component<Transform3DComponent>()->position.x = 3;
  world.entities.push_back(std::move(trigger)); world.entities.push_back(std::move(wall));
  const auto verify = [&] {
    const auto any = sphereCast3D(world, {0,0.5F,0}, 0.1F, {1,0,0}, 5);
    const auto solid = sphereCast3D(world, {0,0.5F,0}, 0.1F, {1,0,0}, 5, {}, {}, false);
    return any && any->entityId == "sensor" && any->isTrigger &&
           solid && solid->entityId == "box_wall" && !solid->isTrigger;
  };
  if (!verify()) { std::cerr << "Fallback solid-only sphere cast failed.\n"; return false; }
  ensurePhysicsWorld3D(world).step(world, 1.0F/60);
  if (!verify()) { std::cerr << "Native solid-only sphere cast failed.\n"; return false; }
  return true;
}

bool testCharacterStepAndMovingPlatform() {
  World stepWorld;
  stepWorld.entities.push_back(
      box("floor", {0.0F, -0.5F, 0.0F}, {10.0F, 1.0F, 10.0F}, "static"));
  stepWorld.entities.push_back(
      box("step", {1.0F, 0.1F, 0.0F}, {0.5F, 0.2F, 2.0F}, "static"));
  Entity walker;
  walker.id = "walker";
  walker.setComponent<Transform3DComponent>(
      Transform3DComponent{.position = {-1.0F, 1.0F, 0.0F}});
  walker.setComponent<CharacterController3DComponent>(
      CharacterController3DComponent{.stepHeight = 0.3F});
  walker.setComponent<CapsuleCollider3DComponent>(
      CapsuleCollider3DComponent{.radius = 0.4F, .height = 1.8F});
  stepWorld.entities.push_back(std::move(walker));
  for (int step = 0; step < 120; ++step) {
    if (!setCharacterVelocity3D(stepWorld, "walker", {2.0F, 0.0F, 0.0F}))
      return false;
    stepPhysics3D(stepWorld, 1.0F / 60.0F);
  }
  if (findEntity(stepWorld, "walker")
          ->component<Transform3DComponent>()
          ->position.x < 1.5F) {
    std::cerr << "Character controller did not climb an allowed step.\n";
    return false;
  }

  World platformWorld;
  platformWorld.entities.push_back(
      box("platform", {0.0F, 0.0F, 0.0F}, {3.0F, 0.4F, 3.0F}, "kinematic"));
  Entity rider;
  rider.id = "rider";
  rider.setComponent<Transform3DComponent>(
      Transform3DComponent{.position = {0.0F, 1.1F, 0.0F}});
  rider.setComponent<CharacterController3DComponent>(
      CharacterController3DComponent{});
  rider.setComponent<CapsuleCollider3DComponent>(
      CapsuleCollider3DComponent{.radius = 0.4F, .height = 1.8F});
  platformWorld.entities.push_back(std::move(rider));
  for (int step = 0; step < 30; ++step)
    stepPhysics3D(platformWorld, 1.0F / 60.0F);
  for (int step = 1; step <= 120; ++step) {
    if (!moveKinematicBody3D(platformWorld, "platform",
                             {static_cast<float>(step) / 60.0F, 0.0F, 0.0F}, {},
                             1.0F / 60.0F))
      return false;
    stepPhysics3D(platformWorld, 1.0F / 60.0F);
  }
  const auto riderState = characterState3D(platformWorld, "rider");
  const float riderX = findEntity(platformWorld, "rider")
                           ->component<Transform3DComponent>()
                           ->position.x;
  if (!riderState || !riderState->grounded || riderX < 1.0F) {
    std::cerr << "Character controller did not inherit moving-platform "
                 "motion.\n";
    return false;
  }
  return true;
}

bool testRepeatedLifetimeAndInterpolation() {
  for (int cycle = 0; cycle < 32; ++cycle) {
    World world;
    world.entities.push_back(
        box("body", {}, {1.0F, 1.0F, 1.0F}, "dynamic", {1.0F, 0.0F, 0.0F}));
    stepPhysics3D(world, 1.0F / 60.0F, {});
    stepPhysics3D(world, 1.0F / 60.0F, {});
    const auto before =
        world.physicsWorld3D->interpolatedPosition("body", -2.0F);
    const auto after = world.physicsWorld3D->interpolatedPosition("body", 3.0F);
    if (!before || !after || after->x < before->x) {
      std::cerr << "Fixed-step interpolation did not clamp safely.\n";
      return false;
    }
    world.entities.clear();
    stepPhysics3D(world, 1.0F / 60.0F, {});
  }
  return true;
}

struct ReplayResult {
  Vec3 position{};
  Vec3 velocity{};
  bool commandsAccepted{true};
};

ReplayResult runDeterministicReplay() {
  ReplayResult result;
  World world;
  world.entities.push_back(
      box("floor", {0.0F, -0.5F, 0.0F}, {20.0F, 1.0F, 20.0F}, "static"));
  Entity body = box("body", {-2.0F, 3.0F, 0.5F}, {0.75F, 0.75F, 0.75F},
                    "dynamic", {3.0F, 0.0F, -1.0F}, false, true);
  body.component<Rigidbody3DComponent>()->linearDamping = 0.08F;
  body.component<Rigidbody3DComponent>()->angularDamping = 0.12F;
  world.entities.push_back(std::move(body));

  for (int step = 0; step < 240; ++step) {
    if (step == 20)
      result.commandsAccepted &=
          addRigidbodyImpulse3D(world, "body", {0.5F, 1.25F, 0.75F});
    if (step >= 60 && step < 90)
      result.commandsAccepted &=
          addRigidbodyForce3D(world, "body", {-0.25F, 0.0F, 0.5F});
    stepPhysics3D(world, 1.0F / 60.0F, {});
  }

  const Entity *bodyEntity = findEntity(world, "body");
  result.position = bodyEntity->component<Transform3DComponent>()->position;
  result.velocity = rigidbodyVelocity3D(world, "body").value_or(Vec3{});
  return result;
}

bool testDeterministicReplay() {
  const ReplayResult first = runDeterministicReplay();
  const ReplayResult second = runDeterministicReplay();
  const auto same = [](const Vec3 left, const Vec3 right) {
    return near(left.x, right.x, 0.00001F) && near(left.y, right.y, 0.00001F) &&
           near(left.z, right.z, 0.00001F);
  };
  if (!first.commandsAccepted || !second.commandsAccepted ||
      !same(first.position, second.position) ||
      !same(first.velocity, second.velocity)) {
    std::cerr << "Identical fixed-step 3D inputs produced different replay "
                 "results.\n";
    return false;
  }
  return true;
}

class PhysicsPreparationProfile {
public:
  PhysicsPreparationProfile() {
    RuntimeProfiler::setEnabled(true);
    RuntimeProfiler::resetSession();
  }
  ~PhysicsPreparationProfile() { RuntimeProfiler::setEnabled(false); }
};

int physicsProfileCalls(const std::string &name) {
  const auto entries = RuntimeProfiler::sessionEntries();
  const auto found =
      std::ranges::find(entries, name, &RuntimeProfiler::Entry::name);
  return found == entries.end() ? 0 : found->calls;
}

double physicsProfileGauge(const std::string &name) {
  const auto entries = RuntimeProfiler::sessionEntries();
  const auto found =
      std::ranges::find(entries, name, &RuntimeProfiler::Entry::name);
  return found == entries.end() || !found->hasGauge ? -1.0 : found->gauge;
}

bool prepareOnWorker(World &world, std::string &error) {
  auto preparation = std::async(
      std::launch::async, [&] { return preparePhysicsShapes3D(world, error); });
  return preparation.get();
}

World sharedModelColliderScene() {
  World world;
  ColliderAsset3D source;
  source.size = {2.0F, 0.01F, 2.0F};
  source.revision = 1;
  source.triangles = {
      {{-1, 0, -1}, {-1, 0, 1}, {1, 0, 1}},
      {{-1, 0, -1}, {1, 0, 1}, {1, 0, -1}},
  };
  world.colliderAssets3D.emplace("asset://shared-plane", std::move(source));
  const std::array<Vec3, 3> scales{Vec3{1, 1, 1}, Vec3{2, 1, 3},
                                   Vec3{0.5F, 2, 0.75F}};
  for (std::size_t index = 0; index < scales.size(); ++index) {
    Entity entity;
    entity.id = "plane_" + std::to_string(index);
    Transform3DComponent transform;
    transform.position = {float(index) * 10.0F, 0, 0};
    transform.scale = scales[index];
    entity.setComponent(transform);
    ModelCollider3DComponent collider;
    collider.asset = "asset://shared-plane";
    collider.layer = "prepared";
    entity.setComponent(collider);
    world.entities.push_back(std::move(entity));
  }
  return world;
}

bool testPreparedShapesAndScaledModelReuse() {
  World world = sharedModelColliderScene();
  std::string error;
  if (!prepareOnWorker(world, error) || !world.preparedPhysicsShapes3D ||
      world.physicsWorld3D) {
    std::cerr
        << "Worker shape preparation must own shapes without a simulation: "
        << error << '\n';
    return false;
  }
  PhysicsPreparationProfile profile;
  stepPhysics3D(world, 1.0F / 60.0F, {});
  if (world.preparedPhysicsShapes3D ||
      physicsProfileCalls("Physics3D.create_shape") != 0 ||
      physicsProfileGauge("Physics3D.prepared_shapes") != 3 ||
      physicsProfileGauge("Physics3D.prepared_model_geometries") != 1) {
    std::cerr << "Prepared instances must adopt three shapes from one model "
                 "geometry without recooking.\n";
    return false;
  }
  auto &physics = ensurePhysicsWorld3D(world);
  const auto wideHit =
      physics.raycast({11.5F, 3, 0}, {0, -1, 0}, 5, "prepared");
  const auto longHit =
      physics.raycast({10, 3, 2.5F}, {0, -1, 0}, 5, "prepared");
  const auto outside =
      physics.raycast({12.5F, 3, 0}, {0, -1, 0}, 5, "prepared");
  if (!wideHit || !longHit || outside || wideHit->entityId != "plane_1" ||
      !near(wideHit->point.y, 0.0F, 0.001F)) {
    std::cerr << "Shared model decorations must preserve nonuniform scaled "
                 "collision bounds.\n";
    return false;
  }
  findEntity(world, "plane_2")->component<Transform3DComponent>()->scale.x =
      0.75F;
  stepPhysics3D(world, 1.0F / 60.0F, {});
  if (physicsProfileCalls("Physics3D.create_shape") != 1) {
    std::cerr << "A live scale change must invalidate exactly its native body "
                 "shape.\n";
    return false;
  }
  auto &source = world.colliderAssets3D.at("asset://shared-plane");
  for (auto &triangle : source.triangles) {
    triangle.a.y = 0.5F;
    triangle.b.y = 0.5F;
    triangle.c.y = 0.5F;
  }
  ++source.revision;
  stepPhysics3D(world, 1.0F / 60.0F, {});
  const auto changed = physics.raycast({20, 3, 0}, {0, -1, 0}, 5, "prepared");
  if (physicsProfileCalls("Physics3D.create_shape") != 4 || !changed ||
      !near(changed->point.y, 1.0F, 0.001F)) {
    std::cerr << "Model revision changes must refresh every shared scaled "
                 "instance.\n";
    return false;
  }
  return true;
}

bool testPreparedCompoundScalePreservesPartsAndFilters() {
  World world;
  ColliderAsset3D source;
  source.revision = 1;
  ColliderPart3D part;
  part.id = "offset_block";
  part.density = 2400.0F;
  for (const float x : {0.5F, 1.5F}) {
    for (const float y : {-0.5F, 0.5F}) {
      for (const float z : {-0.5F, 0.5F}) {
        part.points.push_back({x, y, z});
      }
    }
  }
  source.parts.push_back(std::move(part));
  world.colliderAssets3D.emplace("asset://compound", std::move(source));
  for (int index = 0; index < 2; ++index) {
    Entity entity;
    entity.id = index == 0 ? "positive_compound" : "negative_compound";
    Transform3DComponent transform;
    transform.position = {index * 10.0F, 0, 0};
    transform.scale = {index == 0 ? 2.0F : -2.0F, 1, 1};
    entity.setComponent(transform);
    ModelCollider3DComponent collider;
    collider.asset = "asset://compound";
    collider.layer = "compound_layer";
    collider.isTrigger = index == 1;
    entity.setComponent(collider);
    world.entities.push_back(std::move(entity));
  }
  std::string error;
  if (!prepareOnWorker(world, error)) {
    std::cerr << "Compound shape preparation failed: " << error << '\n';
    return false;
  }
  PhysicsPreparationProfile profile;
  stepPhysics3D(world, 1.0F / 60.0F, {});
  auto &physics = ensurePhysicsWorld3D(world);
  const auto positive =
      physics.raycast({2, 3, 0}, {0, -1, 0}, 5, "compound_layer");
  const auto negative =
      physics.raycast({8, 3, 0}, {0, -1, 0}, 5, "compound_layer");
  if (!positive || !negative || positive->colliderPartId != "offset_block" ||
      negative->colliderPartId != "offset_block" || positive->isTrigger ||
      !negative->isTrigger ||
      physicsProfileGauge("Physics3D.prepared_model_geometries") != 1 ||
      physicsProfileCalls("Physics3D.create_shape") != 0) {
    std::cerr << "Scaled compound reuse changed signed geometry, stable part "
                 "IDs, or trigger/layer policy.\n";
    return false;
  }
  return true;
}

bool testPreparedShapeSignatureInvalidation() {
  World world;
  world.entities.push_back(box("prepared_box", {}, {1, 1, 1}, "static"));
  std::string error;
  if (!prepareOnWorker(world, error)) {
    return false;
  }
  auto *collider = world.entities.front().component<BoxCollider3DComponent>();
  collider->size.x = 4.0F;
  PhysicsPreparationProfile profile;
  stepPhysics3D(world, 1.0F / 60.0F, {});
  const auto hit =
      ensurePhysicsWorld3D(world).raycast({1.5F, 3, 0}, {0, -1, 0}, 5);
  if (!hit || physicsProfileCalls("Physics3D.create_shape") != 1) {
    std::cerr << "A collider edited after preparation must not adopt its stale "
                 "shape.\n";
    return false;
  }
  return true;
}

bool testAdditivePreparedShapesRetainRunningWorld() {
  World active;
  active.entities.push_back(box("existing", {0, 0, 0}, {1, 1, 1}, "static"));
  stepPhysics3D(active, 1.0F / 60.0F, {});
  const auto originalPhysics = active.physicsWorld3D;
  World incoming;
  incoming.entities.push_back(box("incoming", {5, 0, 0}, {2, 2, 2}, "static"));
  std::string error;
  if (!prepareOnWorker(incoming, error)) {
    return false;
  }
  PhysicsPreparationProfile profile;
  adoptPreparedPhysicsShapes3D(active, incoming);
  active.entities.push_back(std::move(incoming.entities.front()));
  stepPhysics3D(active, 1.0F / 60.0F, {});
  const auto existing = originalPhysics->raycast({0, 3, 0}, {0, -1, 0}, 5);
  const auto added = originalPhysics->raycast({5, 3, 0}, {0, -1, 0}, 5);
  if (active.physicsWorld3D != originalPhysics ||
      incoming.preparedPhysicsShapes3D ||
      physicsProfileCalls("Physics3D.create_shape") != 0 || !existing ||
      !added || existing->entityId != "existing" ||
      added->entityId != "incoming") {
    std::cerr << "Additive adoption must retain the live physics owner and use "
                 "prepared incoming shapes.\n";
    return false;
  }
  return true;
}

bool testAbandonedAndFailedShapePreparation() {
  World active;
  active.entities.push_back(box("moving", {}, {1, 1, 1}, "dynamic", {1, 0, 0}));
  stepPhysics3D(active, 1.0F / 60.0F, {});
  const auto originalPhysics = active.physicsWorld3D;
  const float before =
      active.entities.front().component<Transform3DComponent>()->position.x;
  for (int attempt = 0; attempt < 8; ++attempt) {
    std::weak_ptr<PreparedPhysicsShapes3D> retired;
    {
      World cancelled;
      cancelled.entities.push_back(box("cancelled", {}, {1, 1, 1}, "static"));
      std::string error;
      if (!prepareOnWorker(cancelled, error)) {
        return false;
      }
      retired = cancelled.preparedPhysicsShapes3D;
    }
    if (!retired.expired()) {
      std::cerr
          << "Discarded scene preparation retained its native shape owner.\n";
      return false;
    }
  }
  World failed;
  failed.entities.push_back(box("valid_first", {}, {1, 1, 1}, "static"));
  Entity invalid;
  invalid.id = "invalid_hull";
  invalid.setComponent(Transform3DComponent{});
  invalid.setComponent(ConvexCollider3DComponent{});
  failed.entities.push_back(std::move(invalid));
  std::string error;
  if (prepareOnWorker(failed, error) || failed.preparedPhysicsShapes3D ||
      failed.physicsWorld3D ||
      error.find("invalid_hull") == std::string::npos) {
    std::cerr
        << "Failed native shape preparation published incomplete state.\n";
    return false;
  }
  stepPhysics3D(active, 1.0F / 60.0F, {});
  if (active.physicsWorld3D != originalPhysics ||
      active.entities.front().component<Transform3DComponent>()->position.x <=
          before) {
    std::cerr << "Cancelled or failed shape preparation disturbed the running "
                 "simulation.\n";
    return false;
  }
  return true;
}

bool testColliderShapeCachingAndInvalidation() {
  RuntimeProfiler::setEnabled(true);
  RuntimeProfiler::resetSession();

  World world;
  world.entities.push_back(
      box("floor", {0.0F, -0.5F, 0.0F}, {10.0F, 1.0F, 10.0F}, "static"));
  world.entities.push_back(
      box("body", {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "dynamic"));
  stepPhysics3D(world, 1.0F / 60.0F, {});
  stepPhysics3D(world, 1.0F / 60.0F, {});

  const auto entries = RuntimeProfiler::sessionEntries();
  const auto errors = std::ranges::find(entries, "Physics3D.update_error_steps",
                                       &RuntimeProfiler::Entry::name);
  if (errors == entries.end() || !errors->hasGauge || errors->gauge != 0) {
    std::cerr << "Missing or nonzero physics update error telemetry\n";
    RuntimeProfiler::setEnabled(false);
    return false;
  }

  const auto shapeBuildCalls = [] {
    const auto entries = RuntimeProfiler::sessionEntries();
    const auto found = std::ranges::find(entries, "Physics3D.create_shape",
                                         &RuntimeProfiler::Entry::name);
    return found == entries.end() ? 0 : found->calls;
  };
  if (shapeBuildCalls() != 2) {
    std::cerr << "Unchanged 3D collider shapes were rebuilt between fixed "
                 "steps.\n";
    RuntimeProfiler::setEnabled(false);
    return false;
  }

  findEntity(world, "body")->component<BoxCollider3DComponent>()->size.x = 1.5F;
  stepPhysics3D(world, 1.0F / 60.0F, {});
  if (shapeBuildCalls() != 3) {
    std::cerr << "Changed 3D collider shape did not invalidate its cached "
                 "physics body.\n";
    RuntimeProfiler::setEnabled(false);
    return false;
  }

  findEntity(world, "body")
      ->component<Rigidbody3DComponent>()
      ->solverVelocitySteps = 32;
  stepPhysics3D(world, 1.0F / 60.0F, {});
  if (shapeBuildCalls() != 4) {
    std::cerr << "Solver quality change did not refresh its native body\n";
    RuntimeProfiler::setEnabled(false);
    return false;
  }

  RuntimeProfiler::setEnabled(false);
  return true;
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

  if (!testNonphysicalBodyInputRejection() ||
      !testStaticHierarchyPoseSynchronization() ||
      !testParentedDynamicPoseOwnership() ||
      !testNativeColliderOffsetSynchronization() ||
      !testSleepingAndWakeCommands() || !testSleepingSupportChanges() ||
      !testSleepingEditsAndReusedBodySlots() ||
      !testContactIdentityAndEntityLifetime() || !testFixedStepSimulation() ||
      !testCollisionFromEverySide() || !testContinuousCollisionAndCommands() ||
      !testTriggersAndFiltering() || !testCharacterAndCameraMath() ||
      !testCharacterUsesSelectedBoxCollider() ||
      !testOffsetCharacterRetainsSupportAfterMoving() ||
      !testAirborneJumpRequestIsNotBufferedByPhysics() ||
      !testShapeQueriesAndColliderKinds() || !testSolidOnlySphereCast() ||
      !testCharacterStepAndMovingPlatform() ||
      !testRepeatedLifetimeAndInterpolation() || !testDeterministicReplay() ||
      !testColliderShapeCachingAndInvalidation() ||
      !testPreparedShapesAndScaledModelReuse() ||
      !testPreparedShapeSignatureInvalidation() ||
      !testPreparedCompoundScalePreservesPartsAndFilters() ||
      !testAdditivePreparedShapesRetainRunningWorld() ||
      !testAbandonedAndFailedShapePreparation())
    return 1;

  return 0;
}
