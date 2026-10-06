#include "demi/runtime/scene/WorldCommandBuffer.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace demi::runtime;

namespace {
using Clock = std::chrono::steady_clock;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::string idFor(std::size_t index) {
  return "scatter/" + std::to_string(index);
}

double milliseconds(Clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

void checkPendingContract() {
  World world;
  WorldCommandBuffer commands;
  Entity entity;
  entity.id = "pending";
  require(commands.create(world, std::move(entity)), "Pending create failed");
  Entity duplicate;
  duplicate.id = "pending";
  require(!commands.create(world, std::move(duplicate)),
          "Duplicate pending ID was accepted");
  require(commands.addComponent(world, "pending", "Transform3D",
                                {{"position", {1, 2, 3}}}),
          "Pending component add failed");
  require(commands.setEnabled(world, "pending", false),
          "Pending enable change failed");
  const auto *pending = commands.pendingEntity("pending");
  require(pending && !pending->enabled &&
              pending->component<Transform3DComponent>() != nullptr,
          "Pending changes were not visible before flush");
  const auto mutations = commands.flush(world);
  require(mutations.size() == 1 &&
              mutations.front().kind == WorldMutationKind::Created &&
              world.entities.size() == 1 && !world.entities.front().enabled &&
              world.entities.front().component<Transform3DComponent>() != nullptr,
          "Pending changes did not commit with the entity");
}

void measure(std::size_t count) {
  World world;
  WorldCommandBuffer commands;
  const auto queueStart = Clock::now();
  for (std::size_t index = 0; index < count; ++index) {
    Entity entity;
    entity.id = idFor(index);
    entity.setComponent(Transform3DComponent{});
    require(commands.create(world, std::move(entity)), "Create was rejected");
    Entity *pending = commands.pendingEntity(idFor(index));
    require(pending != nullptr, "Pending entity was not found");
    pending->component<Transform3DComponent>()->position.x = float(index);
  }
  const auto queueEnd = Clock::now();
  const auto mutations = commands.flush(world);
  const auto flushEnd = Clock::now();

  require(mutations.size() == count, "Flush lost a queued entity");
  require(world.entities.size() == count, "Flush changed entity count");
  for (std::size_t index = 0; index < count; ++index) {
    require(mutations[index].kind == WorldMutationKind::Created &&
                mutations[index].entityId == idFor(index),
            "Mutation order or stable ID changed");
    require(world.entities[index].id == idFor(index) &&
                world.entities[index]
                        .component<Transform3DComponent>()->position.x ==
                    float(index),
            "Entity order or payload changed");
  }
  std::cout << count << ',' << milliseconds(queueEnd - queueStart) << ','
            << milliseconds(flushEnd - queueEnd) << '\n';
}
} // namespace

int main() {
  try {
    checkPendingContract();
    std::cout << "entities,queue_ms,flush_ms\n";
    for (const std::size_t count : {1000U, 2000U, 4000U})
      measure(count);
  } catch (const std::exception &error) {
    std::cerr << "World command buffer benchmark failed: " << error.what()
              << '\n';
    return 1;
  }
}
