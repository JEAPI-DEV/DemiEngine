#include "demi/runtime/scene/model/EntityCollection.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <stdexcept>
#include <utility>

using namespace demi::runtime;

namespace {
struct Position {
  int value = 0;
};

struct ThrowingCopy {
  inline static bool fail = false;
  ThrowingCopy() = default;
  ThrowingCopy(const ThrowingCopy &) {
    if (fail) {
      throw std::runtime_error("copy failure");
    }
  }
  ThrowingCopy(ThrowingCopy &&) noexcept = default;
  ThrowingCopy &operator=(const ThrowingCopy &) = default;
  ThrowingCopy &operator=(ThrowingCopy &&) noexcept = default;
};

Entity entity(const char *id, int value) {
  Entity result;
  result.id = id;
  result.setComponent(Position{value});
  return result;
}

void migrationAndSnapshots() {
  EntityCollection source;
  source.push_back(entity("first", 7));
  source.push_back(entity("second", 11));
  assert(source[0].components.owner() == source[1].components.owner());
  assert(&source.componentRegistry() == source[0].components.owner().get());

  EntityCollection snapshot(source);
  snapshot[0].component<Position>()->value = 99;
  assert(source[0].component<Position>()->value == 7);
  assert(snapshot[0].components.owner() != source[0].components.owner());

  EntityCollection destination;
  destination.push_back(std::move(source[0]));
  assert(destination[0].component<Position>()->value == 7);
  assert(destination[0].components.owner().get() ==
         &destination.componentRegistry());

  const auto *registry = &snapshot.componentRegistry();
  const auto handle = snapshot[0].components.handle();
  EntityCollection moved(std::move(snapshot));
  assert(&moved.componentRegistry() == registry);
  assert(moved.indexOf(handle) == 0);
  snapshot.push_back(entity("reused", 3));
  assert(snapshot[0].component<Position>()->value == 3);
}

void assignmentAndRollback() {
  EntityCollection original{entity("original", 7)};
  EntityCollection rollback;
  rollback = original;
  const auto originalHandle = original[0].components.handle();
  original.replace(0, entity("replacement", 9));
  assert(!original.indexOf(originalHandle));
  original = std::move(rollback);
  assert(original[0].id == "original");
  assert(original[0].component<Position>()->value == 7);
  assert(original.indexOf(original[0].components.handle()) == 0);
  rollback.push_back(entity("reused", 3));
  assert(rollback.indexOf(rollback[0].components.handle()) == 0);
}

void unpublishedAdoptionAndFailure() {
  EntityCollection entities{entity("original", 7)};
  const auto originalHandle = entities[0].components.handle();
  Entity staged = entity("staged", 9);
  const auto beforeAdoption = entities.orderRevision();
  entities.prepareAdoption(staged);
  assert(entities.orderRevision() == beforeAdoption);
  assert(!entities.indexOf(staged.components.handle()));
  assert(entities.size() == 1);
  entities.push_back(std::move(staged));
  assert(entities.indexOf(entities[1].components.handle()) == 1);

  Entity failing;
  failing.setComponent(ThrowingCopy{});
  ThrowingCopy::fail = true;
  bool failed = false;
  try {
    entities.prepareAdoption(failing);
  } catch (const std::runtime_error &) {
    failed = true;
  }
  ThrowingCopy::fail = false;
  assert(failed);
  assert(entities.size() == 2);
  assert(entities.indexOf(originalHandle) == 0);
  assert(entities[0].component<Position>()->value == 7);
  assert(failing.hasComponent<ThrowingCopy>());
}

void orderedMutation() {
  EntityCollection entities{entity("a", 1), entity("b", 2), entity("c", 3)};
  const auto first = entities[0].components.handle();
  const auto beforeSwap = entities.orderRevision();
  const auto beforeSwapStorage = entities.componentRegistry().storageRevision();
  entities.swapElements(0, 2);
  assert(entities.orderRevision() != beforeSwap);
  assert(entities.componentRegistry().storageRevision() == beforeSwapStorage);
  assert(entities.indexOf(first) == 2);
  entities.eraseIf([](const Entity &value) { return value.id == "b"; });
  assert(entities.indexOf(first) == 1);
  const auto replacedHandle = entities[0].components.handle();
  entities.replace(0, entity("replacement", 9));
  assert(!entities.indexOf(replacedHandle));
  assert(entities.indexOf(entities[0].components.handle()) == 0);
  assert(entities[0].component<Position>()->value == 9);
  assert(entities[0].components.owner() == entities[1].components.owner());
  entities.insert(entities.begin(), entity("inserted", 4));
  assert(entities.indexOf(first) == 2);
  entities.reserve(128);
  assert(entities.indexOf(first) == 2);
  entities.resize(1);
  assert(!entities.indexOf(first));
}
} // namespace

int main() {
  migrationAndSnapshots();
  orderedMutation();
  assignmentAndRollback();
  unpublishedAdoptionAndFailure();
}
