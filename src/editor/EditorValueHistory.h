#pragma once

#include <type_traits>
#include <utility>
#include <vector>

namespace demi::editor {

enum class EditorHistoryResult { Applied, Empty, Conflict };

// Undo/Redo for value snapshots. Identity, validation, dirty state and source
// persistence belong to the document using it, not this history container.
template <typename Value> class EditorValueHistory {
public:
  static_assert(std::is_nothrow_swappable_v<Value>,
                "History values need nonthrowing swap for atomic application.");

  [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
  [[nodiscard]] std::size_t undoCount() const { return undo_.size(); }
  [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
  void clear() {
    undo_.clear();
    redo_.clear();
  }

  [[nodiscard]] bool record(const Value &before, const Value &after,
                            bool coalesce = false) {
    if (before == after)
      return false;
    if (coalesce && !undo_.empty() && redo_.empty() &&
        undo_.back().after == before) {
      Value replacement(after);
      using std::swap;
      swap(undo_.back().after, replacement);
      if (undo_.back().before == after)
        undo_.pop_back();
      return true;
    }
    undo_.push_back({before, after});
    redo_.clear();
    return true;
  }

  [[nodiscard]] EditorHistoryResult undo(Value &current) {
    return apply(current, undo_, redo_, true);
  }
  [[nodiscard]] EditorHistoryResult redo(Value &current) {
    return apply(current, redo_, undo_, false);
  }

private:
  struct Change {
    Value before;
    Value after;
  };
  static EditorHistoryResult apply(Value &current, std::vector<Change> &source,
                                   std::vector<Change> &destination,
                                   bool undo) {
    if (source.empty())
      return EditorHistoryResult::Empty;
    const auto &change = source.back();
    if (current != (undo ? change.after : change.before))
      return EditorHistoryResult::Conflict;
    // Complete allocations/copies before changing either the value or source
    // stack. A conflict leaves both stacks available for the owner to resolve.
    Value replacement = undo ? change.before : change.after;
    destination.push_back(change);
    using std::swap;
    swap(current, replacement);
    source.pop_back();
    return EditorHistoryResult::Applied;
  }

  std::vector<Change> undo_;
  std::vector<Change> redo_;
};

} // namespace demi::editor
