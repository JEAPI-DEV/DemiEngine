#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/scene/WorldQueries.h"

#include <cmath>
#include <iostream>

namespace {

demi::runtime::Entity makePlatform() {
  demi::runtime::Entity platform;
  platform.id = "platform";
  platform.transform2D =
      demi::runtime::Transform2DComponent{.position = {0.0F, 0.0F}};
  platform.rigidbody2D = demi::runtime::Rigidbody2DComponent{
      .bodyType = "static", .gravityScale = 0.0F};
  platform.boxCollider2D = demi::runtime::BoxCollider2DComponent{
      .size = {4.0F, 0.45F}, .layer = "platform"};
  return platform;
}

demi::runtime::Entity makePlayer() {
  demi::runtime::Entity player;
  player.id = "player";
  player.transform2D =
      demi::runtime::Transform2DComponent{.position = {2.1F, 0.7F}};
  player.rigidbody2D = demi::runtime::Rigidbody2DComponent{
      .bodyType = "dynamic",
      .velocity = {6.0F, -8.0F},
      .gravityScale = 0.0F,
      .bounciness = 0.55F,
  };
  player.boxCollider2D = demi::runtime::BoxCollider2DComponent{
      .size = {1.0F, 1.0F}, .layer = "player"};
  return player;
}

} // namespace

int main() {
  demi::runtime::World world;
  world.entities.push_back(makePlayer());
  world.entities.push_back(makePlatform());

  demi::runtime::stepPhysics2D(
      world, 1.0F / 60.0F,
      demi::runtime::PhysicsSettings2D{.gravity = {0.0F, 0.0F}});

  const demi::runtime::Entity *player =
      demi::runtime::findEntity(world, "player");
  if (player == nullptr || !player->transform2D.has_value() ||
      !player->rigidbody2D.has_value()) {
    std::cerr << "Player was not available after physics step.\n";
    return 1;
  }

  if (player->transform2D->position.x < 1.5F) {
    std::cerr << "Right-edge top contact snapped player across platform to x="
              << player->transform2D->position.x << ".\n";
    return 1;
  }

  if (player->rigidbody2D->velocity.y <= 0.0F) {
    std::cerr << "Expected top contact to bounce upward; velocity.y="
              << player->rigidbody2D->velocity.y << ".\n";
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

  return 0;
}
