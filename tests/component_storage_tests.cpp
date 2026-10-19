#include "demi/runtime/scene/EntityComponentQuery.h"
#include "demi/runtime/scene/model/ComponentStorage.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using demi::runtime::ComponentStorage;

namespace {
struct Position {
  int value = 0;
};
struct Velocity {
  int value = 0;
};
struct GameplayData {
  std::string name;
  std::vector<int> values;
};

void distinctTypesAndReplacementRemainChecked() {
  ComponentStorage storage;
  assert(!storage.contains<Position>());
  assert(storage.get<Position>() == nullptr);
  assert(storage.get<Velocity>() == nullptr);
  auto &position = storage.set(Position{7});
  assert(&position == storage.get<Position>());
  storage.set(Velocity{19});
  assert(storage.get<Position>()->value == 7);
  assert(storage.get<Velocity>()->value == 19);
  storage.set(Position{31});
  assert(storage.get<Position>()->value == 31);
  assert(storage.get<Velocity>()->value == 19);
  assert(storage.remove<Position>());
  assert(!storage.remove<Position>());
  assert(storage.get<Position>() == nullptr);
  assert(storage.get<Velocity>()->value == 19);
  storage.clear();
  assert(!storage.contains<Velocity>());
}

void cvrefKeysAndConstAccessKeepExistingSemantics() {
  ComponentStorage storage;
  const Position authored{41};
  storage.set(authored);
  assert(storage.contains<Position>());
  assert(storage.contains<const Position>());
  assert(storage.contains<Position &>());
  assert(storage.contains<const Position &>());
  assert(storage.contains<Position &&>());
  assert(storage.get<const Position>()->value == 41);
  const ComponentStorage &readOnly = storage;
  static_assert(
      std::is_same_v<decltype(readOnly.get<Position>()), const Position *>);
  assert(readOnly.get<Position>()->value == 41);
  assert(readOnly.get<const Position>()->value == 41);
  assert(storage.remove<const Position &>());
  assert(!storage.contains<Position>());
}

void copyAndMovePreserveIndependentAnyValues() {
  ComponentStorage source;
  source.set(Position{5});
  source.set(GameplayData{"original", {1, 2, 3}});
  ComponentStorage copy = source;
  copy.get<Position>()->value = 12;
  copy.get<GameplayData>()->name = "copy";
  copy.get<GameplayData>()->values.push_back(4);
  assert(source.get<Position>()->value == 5);
  assert(source.get<GameplayData>()->name == "original");
  assert(source.get<GameplayData>()->values.size() == 3);
  ComponentStorage assigned;
  assigned.set(Velocity{7});
  assigned = source;
  assert(!assigned.contains<Velocity>());
  assert(assigned.get<GameplayData>()->values ==
         source.get<GameplayData>()->values);
  ComponentStorage moved = std::move(copy);
  assert(moved.get<Position>()->value == 12);
  assert(moved.get<GameplayData>()->values.size() == 4);
  ComponentStorage moveAssigned;
  moveAssigned.set(Velocity{22});
  moveAssigned = std::move(moved);
  assert(!moveAssigned.contains<Velocity>());
  assert(moveAssigned.get<GameplayData>()->name == "copy");
  assert(moveAssigned.remove<GameplayData>());
  assert(source.contains<GameplayData>());
  // Moved-from containers remain valid; their unspecified contents are not
  // part of the contract, so explicitly clear before reusing them.
  moved.clear();
  moved.set(Velocity{91});
  assert(moved.get<Velocity>()->value == 91);
}

void orderedQueriesFollowStructuralMutation() {
  using demi::runtime::Entity;
  using demi::runtime::EntityComponentQuery;
  std::vector<Entity> entities(3);
  entities[0].setComponent(Position{1});
  entities[1].setComponent(Velocity{2});
  entities[2].setComponent(Position{3});
  EntityComponentQuery queries;
  const auto expectPositions = [&](std::vector<std::size_t> expected) {
    assert(queries.queryAny<Position>(entities) == expected);
  };
  expectPositions({0, 2});
  const auto epoch = ComponentStorage::membershipEpoch();
  entities[0].component<Position>()->value = 7;
  entities[0].setComponent(Position{9});
  assert(ComponentStorage::membershipEpoch() == epoch);
  entities[1].setComponent(Position{4});
  expectPositions({0, 1, 2});
  entities[0].removeComponent<Position>();
  expectPositions({1, 2});
  std::swap(entities[0], entities[2]);
  expectPositions({0, 1});
  entities[1].components.clear();
  expectPositions({0});
  entities[1] = entities[0];
  expectPositions({0, 1});
  entities.erase(entities.begin());
  expectPositions({0});
  entities[0].enabled = false;
  expectPositions({0});
  EntityComponentQuery copiedQueries = queries;
  auto copiedEntities = entities;
  copiedEntities[0].removeComponent<Position>();
  assert(copiedQueries.queryAny<Position>(copiedEntities).empty());
  expectPositions({0});
  assert((queries.queryAny<Position, Velocity>(entities) ==
          std::vector<std::size_t>{0}));
  // Same collection address and size, replaced membership.
  entities[0] = Entity{};
  expectPositions({});
  entities.clear();
  expectPositions({});
  entities.resize(2);
  entities[0].setComponent(Position{12});
  expectPositions({0});
  const auto *retainedAllocation = entities.data();
  entities.clear();
  entities.resize(2);
  assert(entities.data() == retainedAllocation);
  expectPositions({});
  entities[1].setComponent(Position{13});
  expectPositions({1});
  std::destroy_at(&entities[1]);
  std::construct_at(&entities[1]);
  expectPositions({});
  // Empty observed slots also need invalidation before same-slot replacement.
  std::destroy_at(&entities[1]);
  std::construct_at(&entities[1]);
  entities[1].setComponent(Position{14});
  expectPositions({1});
}
void temporaryStorageDoesNotInvalidateObservedQueries() {
  using demi::runtime::Entity;
  using demi::runtime::EntityComponentQuery;
  std::vector<Entity> entities(2);
  entities[0].setComponent(Position{1});
  EntityComponentQuery queries;
  assert((queries.queryAny<Position>(entities) == std::vector<std::size_t>{0}));
  auto epoch = ComponentStorage::membershipEpoch();
  {
    ComponentStorage parsed;
    parsed.set(Position{2});
    ComponentStorage copied = entities[0].components;
    copied.set(Velocity{3});
    copied.remove<Position>();
    ComponentStorage moved = std::move(copied);
    parsed = moved;
    parsed.clear();
  }
  assert(ComponentStorage::membershipEpoch() == epoch);
  auto copiedEntities = entities;
  assert(ComponentStorage::membershipEpoch() == epoch);
  copiedEntities[1].setComponent(Position{4});
  assert(ComponentStorage::membershipEpoch() == epoch);
  EntityComponentQuery copiedQueries;
  assert((copiedQueries.queryAny<Position>(copiedEntities) ==
          std::vector<std::size_t>{0, 1}));
  copiedEntities[0].removeComponent<Position>();
  assert(ComponentStorage::membershipEpoch() != epoch);
  assert((copiedQueries.queryAny<Position>(copiedEntities) ==
          std::vector<std::size_t>{1}));
  epoch = ComponentStorage::membershipEpoch();
  entities[1].setComponent(Position{5});
  assert(ComponentStorage::membershipEpoch() != epoch);
  assert(
      (queries.queryAny<Position>(entities) == std::vector<std::size_t>{0, 1}));
}

} // namespace

int main() {
  temporaryStorageDoesNotInvalidateObservedQueries();
  orderedQueriesFollowStructuralMutation();
  distinctTypesAndReplacementRemainChecked();
  cvrefKeysAndConstAccessKeepExistingSemantics();
  copyAndMovePreserveIndependentAnyValues();
}
