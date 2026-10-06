#include "editor/EditorValueHistory.h"

#include <cassert>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using demi::editor::EditorHistoryResult;
using demi::editor::EditorValueHistory;

template <typename Value>
void exercise(Value before, Value after, Value branch) {
  EditorValueHistory<Value> history;
  Value current = before;
  assert(history.undo(current) == EditorHistoryResult::Empty);
  assert(history.record(before, after));
  current = after;
  assert(history.undo(current) == EditorHistoryResult::Applied);
  assert(current == before && !history.canUndo() && history.canRedo());
  assert(!history.record(before, before));
  assert(history.canRedo());
  assert(history.redo(current) == EditorHistoryResult::Applied);
  assert(current == after);
  current = branch;
  assert(history.undo(current) == EditorHistoryResult::Conflict);
  assert(current == branch && history.canUndo() && !history.canRedo());
  current = after;
  assert(history.undo(current) == EditorHistoryResult::Applied);
  assert(history.record(before, branch));
  current = branch;
  assert(!history.canRedo());
  assert(history.undo(current) == EditorHistoryResult::Applied &&
         current == before);
  history.clear();
  assert(!history.canUndo() && !history.canRedo());
}

int main() {
  exercise(std::string("old"), std::string("new"), std::string("other"));
  exercise(std::vector<int>{1, 2}, std::vector<int>{3, 4}, std::vector<int>{5});
  using Json = nlohmann::json;
  exercise(Json{{"material", "stone"}}, Json{{"material", "wood"}},
           Json{{"material", "glass"}});
  exercise(Json{}, Json{{"nodes", Json::array()}}, Json{{"value", 42}});
}
