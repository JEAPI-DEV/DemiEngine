#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <cmath>
#include <iostream>

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
      !triggerWorld.physicsContacts.front().isTrigger) {
    std::cerr << "Trigger contacts were not exposed.\n";
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

  return 0;
}
