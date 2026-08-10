#include "demi/runtime/camera/Camera3DMath.h"
#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/physics/SpatialQuery3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <algorithm>
#include <cmath>
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
  const Vec3 rotation = lookAtRotation3D({}, {1.0F, 0.0F, 0.0F});
  const Vec3 forward =
      forwardDirection3D(WorldTransform3D{.rotation = rotation});
  if (!near(forward.x, 1.0F, 0.001F)) {
    std::cerr << "Transform3D look-at/direction helpers disagree.\n";
    return false;
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

  if (!testFixedStepSimulation() || !testCollisionFromEverySide() ||
      !testContinuousCollisionAndCommands() || !testTriggersAndFiltering() ||
      !testCharacterAndCameraMath() ||
      !testCharacterUsesSelectedBoxCollider() ||
      !testAirborneJumpRequestIsNotBufferedByPhysics() ||
      !testShapeQueriesAndColliderKinds() ||
      !testCharacterStepAndMovingPlatform() ||
      !testRepeatedLifetimeAndInterpolation() || !testDeterministicReplay() ||
      !testColliderShapeCachingAndInvalidation())
    return 1;

  return 0;
}
