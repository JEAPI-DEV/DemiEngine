#include "BlastFixture.h"
#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace demi::runtime;
int main() {
  try {
    for (int iteration = 0; iteration < 20; ++iteration) {
      World world;
      Entity floor;
      floor.id = "floor";
      floor.setComponent<Transform3DComponent>(
          Transform3DComponent{.position = {0, -0.5F, 0}});
      floor.setComponent<BoxCollider3DComponent>(
          BoxCollider3DComponent{.size = {20, 1, 20}});
      floor.setComponent<Rigidbody3DComponent>(
          Rigidbody3DComponent{.bodyType = "static"});
      world.entities.push_back(std::move(floor));
      for (const auto &island : splitBlastFixture(2.0F)) {
        if (island.size() != 1)
          throw std::runtime_error("probe requires single-chunk splits");
        const auto chunk = island.front();
        Entity piece;
        piece.id = "chunk_" + std::to_string(chunk);
        piece.setComponent<Transform3DComponent>(Transform3DComponent{
            .position = {static_cast<float>(chunk) * 2, 4, 0}});
        piece.setComponent<BoxCollider3DComponent>(
            BoxCollider3DComponent{.size = {1, 1, 1}});
        piece.setComponent<Rigidbody3DComponent>(
            Rigidbody3DComponent{.bodyType = "dynamic",
                                 .velocity = {0, -1, 0},
                                 .mass = static_cast<float>(chunk + 1)});
        world.entities.push_back(std::move(piece));
      }
      stepPhysics3D(world, 1.0F / 60.0F);
      auto &physics = ensurePhysicsWorld3D(world);
      for (std::size_t index = 1; index < world.entities.size(); ++index) {
        const auto &id = world.entities[index].id;
        if (!physics.addImpulse(id, {6, 0, 0}))
          throw std::runtime_error("chunk impulse rejected");
        const auto velocity = physics.velocity(id);
        if (!velocity ||
            std::abs(velocity->x - 6.0F / static_cast<float>(index)) > 0.001F)
          throw std::runtime_error("backend mass does not match chunk mass");
        if (!physics.addImpulse(id, {-6, 0, 0}))
          throw std::runtime_error("chunk reverse impulse rejected");
      }
      for (int step = 0; step < 240; ++step)
        stepPhysics3D(world, 1.0F / 60.0F);
      for (std::size_t index = 1; index < world.entities.size(); ++index) {
        const auto &piece = world.entities[index];
        const auto *pose = piece.component<Transform3DComponent>();
        const auto *body = piece.component<Rigidbody3DComponent>();
        if (!pose || !body || !std::isfinite(pose->position.y) ||
            std::abs(pose->position.y - 0.5F) > 0.08F ||
            std::abs(pose->position.x - static_cast<float>(index - 1) * 2) >
                0.1F ||
            body->mass != static_cast<float>(index) ||
            piece.id != "chunk_" + std::to_string(index - 1))
          throw std::runtime_error(
              "split chunk pose, identity, mass, or floor collision failed");
      }
    }
    std::cout << "Blast/Jolt probe passed: split IDs mapped to falling bodies "
                 "in 20 world lifetimes.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
