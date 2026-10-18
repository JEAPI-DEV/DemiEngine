#include "demi/runtime/scene/model/ComponentStorage.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
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
} // namespace

int main() {
  distinctTypesAndReplacementRemainChecked();
  cvrefKeysAndConstAccessKeepExistingSemantics();
  copyAndMovePreserveIndependentAnyValues();
}
