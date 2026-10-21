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

void copyAndMovePreserveIndependentValues() {
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
  demi::runtime::EntityCollection entities;
  entities.resize(3);
  entities[0].setComponent(Position{1});
  entities[1].setComponent(Velocity{2});
  entities[2].setComponent(Position{3});
  EntityComponentQuery queries;
  const auto expectPositions = [&](std::vector<std::size_t> expected) {
    assert(queries.queryAny<Position>(entities) == expected);
  };
  expectPositions({0, 2});
  entities[0].component<Position>()->value = 7;
  entities[0].setComponent(Position{9});
  entities[1].setComponent(Position{4});
  expectPositions({0, 1, 2});
  entities[0].removeComponent<Position>();
  expectPositions({1, 2});
  entities.swapElements(0, 2);
  expectPositions({0, 1});
  entities[1].components.clear();
  expectPositions({0});
  entities.replace(1, entities[0]);
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
  entities.replace(0, Entity{});
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
  entities.replace(1, Entity{});
  expectPositions({});
  // Replacing an empty slot must retain collection ownership.
  entities.replace(1, Entity{});
  entities[1].setComponent(Position{14});
  expectPositions({1});
}
void registryRevisionsAreLocalAndTrackTypedMutation() {
  using demi::runtime::ComponentRegistry;
  auto firstRegistry = std::make_shared<ComponentRegistry>();
  auto secondRegistry = std::make_shared<ComponentRegistry>();
  assert(firstRegistry->identity() != secondRegistry->identity());
  ComponentStorage first;
  first.bind(firstRegistry);
  ComponentStorage second;
  second.bind(secondRegistry);
  const auto secondRevision = secondRegistry->storageRevision();
  auto revision = firstRegistry->storageRevision();
  first.set(Position{1});
  assert(firstRegistry->storageRevision() > revision);
  assert(secondRegistry->storageRevision() == secondRevision);
  revision = firstRegistry->storageRevision();
  first.set(Position{2});
  assert(firstRegistry->storageRevision() > revision);
  revision = firstRegistry->storageRevision();
  assert(first.get<Position>()->value == 2);
  first.get<Position>()->value = 3;
  // Borrowed mutable access is deliberately not a notification boundary.
  assert(firstRegistry->storageRevision() == revision);
  firstRegistry->native().patch<Position>(
      first.handle(), [](Position &value) { value.value = 4; });
  assert(firstRegistry->storageRevision() > revision);
  revision = firstRegistry->storageRevision();
  firstRegistry->native().remove<Position>(first.handle());
  assert(firstRegistry->storageRevision() > revision);
  revision = firstRegistry->storageRevision();
  firstRegistry->native().emplace<Position>(first.handle(), Position{5});
  assert(firstRegistry->storageRevision() > revision);
  revision = firstRegistry->storageRevision();
  ComponentStorage shared;
  shared.bind(firstRegistry);
  shared.set(Position{6});
  assert(firstRegistry->storageRevision() > revision);
  revision = firstRegistry->storageRevision();
  {
    ComponentStorage temporary;
    temporary.set(Position{7});
    ComponentStorage snapshot = first;
    assert(snapshot.owner() == nullptr);
    snapshot.set(Position{8});
  }
  assert(firstRegistry->storageRevision() == revision);
  shared.clear();
  assert(firstRegistry->storageRevision() > revision);
  assert(secondRegistry->storageRevision() == secondRevision);
}

struct Marker {};
struct ImmutableValue {
  const int value;
};

void sharedRegistryOwnsTypedPoolsAndDetachedCopies() {
  auto registry = std::make_shared<demi::runtime::ComponentRegistry>();
  ComponentStorage first;
  first.set(Position{7});
  first.bind(registry);
  ComponentStorage second;
  second.bind(registry);
  second.set(Position{12});
  second.set(Marker{});
  assert(first.owner() == registry);
  assert(second.owner() == registry);
  assert(first.handle() != second.handle());
  assert(&registry->native().get<Position>(first.handle()) ==
         first.get<Position>());
  assert(registry->native().view<Position>().size() == 2);
  assert(second.get<Marker>() != nullptr);

  ComponentStorage snapshot = first;
  assert(snapshot.owner() == nullptr);
  snapshot.get<Position>()->value = 21;
  assert(first.get<Position>()->value == 7);

  auto destination = std::make_shared<demi::runtime::ComponentRegistry>();
  const auto oldHandle = second.handle();
  second.bind(destination);
  assert(!registry->native().valid(oldHandle));
  assert(second.get<Position>()->value == 12);
  assert(second.contains<Marker>());
  assert(registry->native().view<Position>().size() == 1);

  first = second;
  assert(first.owner() == registry);
  assert(first.get<Position>()->value == 12);
  assert(first.contains<Marker>());
  assert(second.owner() == destination);
  first.get<Position>()->value = 31;
  assert(second.get<Position>()->value == 12);

  const auto preservedHandle = first.handle();
  first.clear();
  assert(first.handle() == preservedHandle);
  assert(registry->native().valid(preservedHandle));
  assert(!first.contains<Position>());
  first.set(ImmutableValue{4});
  const auto immutableRevision = registry->storageRevision();
  first.set(ImmutableValue{9});
  assert(registry->storageRevision() > immutableRevision);
  assert(first.get<ImmutableValue>()->value == 9);
  ComponentStorage immutableCopy = first;
  assert(immutableCopy.get<ImmutableValue>()->value == 9);
}

void detachedValuesStayLightweightUntilPublished() {
  std::vector<ComponentStorage> authored(1000);
  for (std::size_t index = 0; index < authored.size(); ++index) {
    auto &storage = authored[index];
    storage.set(Position{static_cast<int>(index)});
    storage.set(GameplayData{"authored", {1, 2, 3}});
    assert(storage.owner() == nullptr);
    assert(storage.handle() == entt::null);
  }
  auto registry = std::make_shared<demi::runtime::ComponentRegistry>();
  for (auto &storage : authored) {
    storage.bind(registry);
    assert(storage.owner() == registry);
    assert(storage.handle() != entt::null);
  }
  assert(registry->native().view<Position>().size() == authored.size());
  ComponentStorage snapshot = authored.front();
  assert(snapshot.owner() == nullptr);
  assert(snapshot.handle() == entt::null);
  snapshot.get<GameplayData>()->values.push_back(4);
  assert(authored.front().get<GameplayData>()->values.size() == 3);
  snapshot.bind(registry);
  assert(snapshot.get<GameplayData>()->values.size() == 4);
  snapshot.bind(nullptr);
  assert(snapshot.owner() == nullptr);
  assert(snapshot.handle() == entt::null);
  assert(snapshot.get<GameplayData>()->values.size() == 4);
}

void registryRowsFollowStorageLifetimeAndMoves() {
  static_assert(std::is_nothrow_move_constructible_v<ComponentStorage>);
  static_assert(std::is_nothrow_move_assignable_v<ComponentStorage>);
  auto registry = std::make_shared<demi::runtime::ComponentRegistry>();
  entt::entity handle = entt::null;
  {
    ComponentStorage storage;
    storage.bind(registry);
    storage.set(Position{8});
    handle = storage.handle();
    ComponentStorage moved = std::move(storage);
    assert(moved.handle() == handle);
    assert(storage.get<Position>() == nullptr);
    storage.set(Velocity{3});
    assert(storage.owner() != registry);
    assert(registry->native().valid(handle));
  }
  assert(!registry->native().valid(handle));
  assert(registry->native().view<Position>().empty());
  ComponentStorage destination;
  destination.bind(registry);
  const auto destinationHandle = destination.handle();
  ComponentStorage source;
  source.set(Position{14});
  auto sourceRegistry = std::make_shared<demi::runtime::ComponentRegistry>();
  source.bind(sourceRegistry);
  const auto sourceHandle = source.handle();
  destination = std::move(source);
  assert(destination.owner() == sourceRegistry);
  assert(destination.handle() == sourceHandle);
  assert(!registry->native().valid(destinationHandle));
  assert(source.owner() == nullptr);
  assert(destination.get<Position>()->value == 14);
}

} // namespace

int main() {
  detachedValuesStayLightweightUntilPublished();
  sharedRegistryOwnsTypedPoolsAndDetachedCopies();
  registryRowsFollowStorageLifetimeAndMoves();
  registryRevisionsAreLocalAndTrackTypedMutation();
  orderedQueriesFollowStructuralMutation();
  distinctTypesAndReplacementRemainChecked();
  cvrefKeysAndConstAccessKeepExistingSemantics();
  copyAndMovePreserveIndependentValues();
}
