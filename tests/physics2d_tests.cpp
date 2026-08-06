#include "demi/runtime/physics/Box2DWorldState.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <cmath>
#include <iostream>

#if DEMI_HAS_BOX2D
#include <box2d/box2d.h>
#endif

namespace {

using namespace demi::runtime;

demi::runtime::Entity makePlatform() {
  demi::runtime::Entity platform;
  platform.id = "platform";
  platform.setComponent<Transform2DComponent>(
      demi::runtime::Transform2DComponent{.position = {0.0F, 0.0F}});
  platform.setComponent<Rigidbody2DComponent>(
      demi::runtime::Rigidbody2DComponent{.bodyType = "static",
                                          .gravityScale = 0.0F});
  platform.setComponent<BoxCollider2DComponent>(
      demi::runtime::BoxCollider2DComponent{.size = {4.0F, 0.45F},
                                            .layer = "platform"});
  return platform;
}

demi::runtime::Entity makePlayer() {
  demi::runtime::Entity player;
  player.id = "player";
  player.setComponent<Transform2DComponent>(
      demi::runtime::Transform2DComponent{.position = {2.1F, 0.7F}});
  player.setComponent<Rigidbody2DComponent>(demi::runtime::Rigidbody2DComponent{
      .bodyType = "dynamic",
      .velocity = {6.0F, -8.0F},
      .gravityScale = 0.0F,
      .bounciness = 0.55F,
  });
  player.setComponent<BoxCollider2DComponent>(
      demi::runtime::BoxCollider2DComponent{.size = {1.0F, 1.0F},
                                            .layer = "player"});
  return player;
}

bool capsuleStopsAtPolygonFromAllSides() {
  struct Probe {
    Vec2 start;
    Vec2 step;
    Vec2 expected;
  };
  const Probe probes[] = {
      {{-2.0F, 0.0F}, {0.05F, 0.0F}, {-0.85F, 0.0F}},
      {{2.0F, 0.0F}, {-0.05F, 0.0F}, {0.85F, 0.0F}},
      {{0.0F, -2.0F}, {0.0F, 0.05F}, {0.0F, -0.95F}},
      {{0.0F, 2.0F}, {0.0F, -0.05F}, {0.0F, 0.95F}},
  };

  for (const Probe &probe : probes) {
    World world;
    Entity mover;
    mover.id = "mover";
    mover.setComponent(Transform2DComponent{.position = probe.start});
    mover.setComponent(Rigidbody2DComponent{.bodyType = "kinematic"});
    mover.setComponent(CapsuleCollider2DComponent{.size = {0.7F, 0.9F}});
    world.entities.push_back(std::move(mover));

    Entity obstacle;
    obstacle.id = "polygon";
    obstacle.setComponent(Transform2DComponent{});
    obstacle.setComponent(PolygonCollider2DComponent{
        .points = {{-0.5F, -0.5F},
                   {0.5F, -0.5F},
                   {0.5F, 0.5F},
                   {-0.5F, 0.5F}}});
    world.entities.push_back(std::move(obstacle));

    for (int step = 0; step < 80; ++step) {
      if (!moveAndSlideKinematic(world, "mover", probe.step))
        return false;
    }
    const Vec2 position =
        findEntity(world, "mover")->component<Transform2DComponent>()->position;
    if (std::abs(position.x - probe.expected.x) > 0.001F ||
        std::abs(position.y - probe.expected.y) > 0.001F) {
      std::cerr << "Capsule crossed polygon from direction (" << probe.step.x
                << ", " << probe.step.y << "); stopped at (" << position.x
                << ", " << position.y << ").\n";
      return false;
    }
  }
  return true;
}

} // namespace

int main() {
  demi::runtime::World world;
  world.entities.push_back(makePlayer());
  world.entities.push_back(makePlatform());
  demi::runtime::Entity circle;
  circle.id = "circle";
  circle.setComponent<Transform2DComponent>(
      Transform2DComponent{.position = {6.0F, 0.0F}});
  circle.setComponent<CircleCollider2DComponent>(
      CircleCollider2DComponent{.radius = 0.5F, .layer = "target"});
  world.entities.push_back(std::move(circle));

  demi::runtime::stepPhysics2D(
      world, 1.0F / 60.0F,
      demi::runtime::PhysicsSettings2D{.gravity = {0.0F, 0.0F}});

  const demi::runtime::Entity *player =
      demi::runtime::findEntity(world, "player");
  if (player == nullptr || !player->hasComponent<Transform2DComponent>() ||
      !player->hasComponent<Rigidbody2DComponent>()) {
    std::cerr << "Player was not available after physics step.\n";
    return 1;
  }

  if (player->component<Transform2DComponent>()->position.x < 1.5F) {
    std::cerr << "Right-edge top contact snapped player across platform to x="
              << player->component<Transform2DComponent>()->position.x << ".\n";
    return 1;
  }

  if (player->component<Rigidbody2DComponent>()->velocity.y <= 0.0F) {
    std::cerr << "Expected top contact to bounce upward; velocity.y="
              << player->component<Rigidbody2DComponent>()->velocity.y << ".\n";
    return 1;
  }

  if (!demi::runtime::overlapBox(
          world, demi::runtime::Vec2{.x = 0.0F, .y = 0.28F},
          demi::runtime::Vec2{.x = 0.78F, .y = 0.10F}, "player")) {
    std::cerr << "Expected platform probe to tolerate small Box2D resting "
                 "separation.\n";
    return 1;
  }

  if (!demi::runtime::hasContact(
          world, "player",
          demi::runtime::PhysicsContactFilter2D{.layer = "platform",
                                                .normalYMin = 0.5F})) {
    std::cerr << "Expected player to cache an upward platform contact after "
                 "physics step.\n";
    return 1;
  }

  const auto overlaps = demi::runtime::overlapCircle(
      world, {.x = 6.0F, .y = 0.0F}, 0.1F, "target");
  const auto hit = demi::runtime::raycast2D(
      world, {.x = 3.0F, .y = 0.0F}, {.x = 1.0F, .y = 0.0F}, 10.0F, "target");
  if (overlaps != std::vector<std::string>{"circle"} || !hit ||
      hit->entityId != "circle" || std::abs(hit->distance - 2.5F) > 0.001F ||
      hit->normal.x > -0.99F) {
    std::cerr << "Circle overlap or shape-accurate raycast failed.\n";
    return 1;
  }

  World rotatedWallWorld;
  Entity rotatedWall;
  rotatedWall.id = "rotated_wall";
  rotatedWall.setComponent(Transform2DComponent{
      .position = {.x = 4.0F, .y = 0.0F}, .rotation = 0.785398163F});
  rotatedWall.setComponent(
      BoxCollider2DComponent{.size = {4.0F, 0.5F}, .layer = "arena_wall"});
  rotatedWallWorld.entities.push_back(std::move(rotatedWall));
  const auto wallHit =
      raycast2D(rotatedWallWorld, {}, {1.0F, 0.0F}, 10.0F, "arena_wall");
  if (!wallHit || wallHit->entityId != "rotated_wall" ||
      std::abs(wallHit->distance - 3.6464466F) > 0.001F ||
      wallHit->normal.x > -0.70F || wallHit->normal.y < 0.70F) {
    std::cerr << "Rotated box raycast did not match the collider shape.\n";
    return 1;
  }

  World wallCollisionWorld;
  Entity movingCircle;
  movingCircle.id = "moving_circle";
  movingCircle.setComponent(Transform2DComponent{});
  movingCircle.setComponent(Rigidbody2DComponent{
      .bodyType = "dynamic",
      .velocity = {6.0F, 0.0F},
      .gravityScale = 0.0F,
  });
  movingCircle.setComponent(CircleCollider2DComponent{.radius = 0.5F});
  wallCollisionWorld.entities.push_back(std::move(movingCircle));
  Entity blockingWall;
  blockingWall.id = "blocking_wall";
  blockingWall.setComponent(
      Transform2DComponent{.position = {.x = 2.0F, .y = 0.0F}});
  blockingWall.setComponent(
      BoxCollider2DComponent{.size = {0.5F, 4.0F}, .layer = "arena_wall"});
  wallCollisionWorld.entities.push_back(std::move(blockingWall));
  for (int step = 0; step < 60; ++step)
    stepPhysics2D(wallCollisionWorld, 1.0F / 60.0F,
                  PhysicsSettings2D{.gravity = {0.0F, 0.0F}});
  const float blockedX = findEntity(wallCollisionWorld, "moving_circle")
                             ->component<Transform2DComponent>()
                             ->position.x;
  if (blockedX > 1.26F) {
    std::cerr << "Dynamic circle passed through a static arena wall: x="
              << blockedX << ".\n";
    return 1;
  }

  World filtered;
  for (int index = 0; index < 2; ++index) {
    Entity entity;
    entity.id = "filtered_" + std::to_string(index);
    entity.setComponent(Transform2DComponent{});
    entity.setComponent(Rigidbody2DComponent{
        .bodyType = index == 0 ? "dynamic" : "static", .gravityScale = 0.0F});
    entity.setComponent(CircleCollider2DComponent{
        .radius = 1.0F,
        .layer = "filtered",
        .categoryBits = static_cast<std::uint16_t>(1U << index),
        .maskBits = 0});
    filtered.entities.push_back(std::move(entity));
  }
  stepPhysics2D(filtered, 1.0F / 60.0F,
                PhysicsSettings2D{.gravity = {0.0F, 0.0F}});
  if (!filtered.physicsContacts.empty()) {
    std::cerr << "Collider category/mask filtering failed.\n";
    return 1;
  }

  World layerWorld;
  layerWorld.physicsCategoryBits = {
      {"blocked", 0x0001}, {"enabled", 0x0001}, {"targetx", 0x0002}};
  layerWorld.physicsMaskBits = {
      {"blocked", 0x0000}, {"enabled", 0x0002}, {"targetx", 0x0001}};
  for (int index = 0; index < 2; ++index) {
    Entity entity;
    entity.id = "layer_" + std::to_string(index);
    entity.setComponent(Transform2DComponent{});
    if (index == 0)
      entity.setComponent(
          Rigidbody2DComponent{.bodyType = "dynamic", .gravityScale = 0.0F});
    entity.setComponent(CircleCollider2DComponent{
        .radius = 1.0F, .layer = index == 0 ? "blocked" : "targetx"});
    layerWorld.entities.push_back(std::move(entity));
  }
  stepPhysics2D(layerWorld, 1.0F / 60.0F,
                PhysicsSettings2D{.gravity = {0.0F, 0.0F}});
  layerWorld.entities.front().component<CircleCollider2DComponent>()->layer =
      "enabled";
  stepPhysics2D(layerWorld, 1.0F / 60.0F,
                PhysicsSettings2D{.gravity = {0.0F, 0.0F}});
  if (layerWorld.physicsContacts.size() != 2) {
    std::cerr << "Changing between equal-length collider layers did not "
                 "refresh Box2D filters.\n";
    return 1;
  }

  World triggerWorld;
  for (int index = 0; index < 2; ++index) {
    Entity entity;
    entity.id = "trigger_" + std::to_string(index);
    entity.setComponent(Transform2DComponent{});
    if (index == 1)
      entity.setComponent(
          Rigidbody2DComponent{.bodyType = "dynamic", .gravityScale = 0.0F});
    entity.setComponent(
        CircleCollider2DComponent{.radius = 1.0F,
                                  .isTrigger = index == 0,
                                  .layer = index == 0 ? "sensor" : "actor"});
    triggerWorld.entities.push_back(std::move(entity));
  }
  stepPhysics2D(triggerWorld, 1.0F / 60.0F,
                PhysicsSettings2D{.gravity = {0.0F, 0.0F}});
  if (triggerWorld.physicsContacts.size() != 2 ||
      !triggerWorld.physicsContacts.front().isTrigger ||
      triggerWorld.physicsContacts.front().phase != "enter") {
    std::cerr << "Trigger contacts were not exposed.\n";
    return 1;
  }
  stepPhysics2D(triggerWorld, 1.0F / 60.0F,
                PhysicsSettings2D{.gravity = {0.0F, 0.0F}});
  if (triggerWorld.physicsContacts.size() != 2 ||
      triggerWorld.physicsContacts.front().phase != "stay") {
    std::cerr << "Trigger stay phase was not retained.\n";
    return 1;
  }
  triggerWorld.entities.back().component<Transform2DComponent>()->position.x =
      5.0F;
  stepPhysics2D(triggerWorld, 1.0F / 60.0F,
                PhysicsSettings2D{.gravity = {0.0F, 0.0F}});
  if (triggerWorld.physicsContacts.size() != 2 ||
      triggerWorld.physicsContacts.front().phase != "exit") {
    std::cerr << "Trigger exit phase was not synthesized.\n";
    return 1;
  }

  World jointWorld;
  Entity anchor;
  anchor.id = "anchor";
  anchor.setComponent(Transform2DComponent{});
  anchor.setComponent(CircleCollider2DComponent{.radius = 0.1F});
  jointWorld.entities.push_back(std::move(anchor));
  Entity bob;
  bob.id = "bob";
  bob.setComponent(Transform2DComponent{.position = {.x = 3.0F, .y = 0.0F}});
  bob.setComponent(
      Rigidbody2DComponent{.bodyType = "dynamic", .gravityScale = 0.0F});
  bob.setComponent(CircleCollider2DComponent{.radius = 0.1F});
  bob.setComponent(
      DistanceJoint2DComponent{.otherEntity = "anchor", .length = 1.0F});
  jointWorld.entities.push_back(std::move(bob));
  for (int step = 0; step < 30; ++step)
    stepPhysics2D(jointWorld, 1.0F / 60.0F,
                  PhysicsSettings2D{.gravity = {0.0F, 0.0F}});
#if DEMI_HAS_BOX2D
  if (jointWorld.box2dState == nullptr ||
      static_cast<b2World *>(jointWorld.box2dState->world)->GetJointCount() !=
          1) {
    std::cerr << "Persistent Box2D world accumulated distance joints.\n";
    return 1;
  }
#endif
  const float jointDistance =
      jointWorld.entities.back().component<Transform2DComponent>()->position.x;
  if (std::abs(jointDistance - 1.0F) > 0.05F) {
    std::cerr << "Distance joint did not enforce its authored length: "
              << jointDistance << ".\n";
    return 1;
  }

  World landingWorld;
  Entity fallingCircle;
  fallingCircle.id = "falling_circle";
  fallingCircle.setComponent(
      Transform2DComponent{.position = {.x = 0.0F, .y = 2.0F}});
  fallingCircle.setComponent(Rigidbody2DComponent{
      .bodyType = "dynamic", .gravityScale = 1.0F, .bounciness = 0.0F});
  fallingCircle.setComponent(CircleCollider2DComponent{.radius = 0.5F});
  landingWorld.entities.push_back(std::move(fallingCircle));
  Entity landingPlatform;
  landingPlatform.id = "landing_platform";
  landingPlatform.setComponent(Transform2DComponent{});
  landingPlatform.setComponent(BoxCollider2DComponent{.size = {8.0F, 0.5F}});
  landingWorld.entities.push_back(std::move(landingPlatform));
  for (int step = 0; step < 120; ++step)
    stepPhysics2D(landingWorld, 1.0F / 60.0F);
  const Entity *landed = findEntity(landingWorld, "falling_circle");
  const float landedY = landed->component<Transform2DComponent>()->position.y;
  if (std::abs(landedY - 0.75F) > 0.06F ||
      std::abs(landed->component<Rigidbody2DComponent>()->velocity.y) > 0.05F) {
    std::cerr << "Dynamic circle did not settle on the platform: y=" << landedY
              << '\n';
    return 1;
  }

  World projectileWorld;
  Entity projectile;
  projectile.id = "projectile";
  projectile.setComponent(Transform2DComponent{});
  projectile.setComponent(Rigidbody2DComponent{
      .bodyType = "dynamic",
      .velocity = {300.0F, 0.0F},
      .gravityScale = 0.0F,
      .continuous = true,
  });
  projectile.setComponent(CircleCollider2DComponent{.radius = 0.05F});
  projectileWorld.entities.push_back(std::move(projectile));
  Entity thinWall;
  thinWall.id = "thin_wall";
  thinWall.setComponent(
      Transform2DComponent{.position = {.x = 2.0F, .y = 0.0F}});
  thinWall.setComponent(BoxCollider2DComponent{.size = {0.1F, 4.0F}});
  projectileWorld.entities.push_back(std::move(thinWall));
  stepPhysics2D(projectileWorld, 1.0F / 60.0F,
                PhysicsSettings2D{.gravity = {0.0F, 0.0F}});
  if (findEntity(projectileWorld, "projectile")
          ->component<Transform2DComponent>()
          ->position.x > 2.0F) {
    std::cerr << "Continuous fast projectile tunneled through a thin wall.\n";
    return 1;
  }

  World shapeWorld;
  Entity capsule;
  capsule.id = "capsule";
  capsule.setComponent(Transform2DComponent{});
  capsule.setComponent(CapsuleCollider2DComponent{.size = {1.0F, 2.0F}});
  shapeWorld.entities.push_back(std::move(capsule));
  Entity polygon;
  polygon.id = "polygon";
  polygon.setComponent(
      Transform2DComponent{.position = {.x = 3.0F, .y = 0.0F}});
  polygon.setComponent(PolygonCollider2DComponent{
      .points = {{-1.0F, -0.5F}, {1.0F, -0.5F}, {0.0F, 1.0F}}});
  shapeWorld.entities.push_back(std::move(polygon));
  Entity chain;
  chain.id = "chain";
  chain.setComponent(Transform2DComponent{});
  chain.setComponent(EdgeCollider2DComponent{
      .points = {{-2.0F, -2.0F}, {0.0F, -1.0F}, {2.0F, -2.0F}}});
  shapeWorld.entities.push_back(std::move(chain));
  stepPhysics2D(shapeWorld, 1.0F / 60.0F,
                PhysicsSettings2D{.gravity = {0.0F, 0.0F}});
#if DEMI_HAS_BOX2D
  if (shapeWorld.box2dState == nullptr ||
      static_cast<b2World *>(shapeWorld.box2dState->world)->GetBodyCount() !=
          3) {
    std::cerr << "Capsule, polygon, and edge/chain fixtures were not built.\n";
    return 1;
  }
#endif

  World kinematicWorld;
  Entity mover;
  mover.id = "mover";
  mover.setComponent(Transform2DComponent{});
  mover.setComponent(Rigidbody2DComponent{.bodyType = "kinematic"});
  mover.setComponent(BoxCollider2DComponent{.size = {1.0F, 1.0F}});
  kinematicWorld.entities.push_back(std::move(mover));
  Entity slideWall;
  slideWall.id = "slide_wall";
  slideWall.setComponent(
      Transform2DComponent{.position = {.x = 2.0F, .y = 0.0F}});
  slideWall.setComponent(BoxCollider2DComponent{.size = {1.0F, 4.0F}});
  kinematicWorld.entities.push_back(std::move(slideWall));
  const auto applied =
      moveAndSlideKinematic(kinematicWorld, "mover", {3.0F, 0.5F});
  const Vec2 moved = findEntity(kinematicWorld, "mover")
                         ->component<Transform2DComponent>()
                         ->position;
  if (!applied || std::abs(applied->x - 1.0F) > 0.001F ||
      std::abs(applied->y - 0.5F) > 0.001F ||
      std::abs(moved.x - 1.0F) > 0.001F || std::abs(moved.y - 0.5F) > 0.001F) {
    std::cerr << "Kinematic move-and-slide did not stop and preserve tangent "
                 "motion.\n";
    return 1;
  }

  if (!capsuleStopsAtPolygonFromAllSides())
    return 1;

  return 0;
}
