#include "demi/runtime/physics/PhysicsContactPhases3D.h"
#include <functional>
#include <memory_resource>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace demi::runtime {
namespace {
struct Pair {
  std::string_view first;
  std::string_view second;
  bool operator==(const Pair &) const = default;
  bool operator<(const Pair &other) const {
    return first < other.first ||
           (first == other.first && second < other.second);
  }
};
Pair key(const PhysicsContact3D &contact) {
  Pair pair{contact.entityId, contact.otherEntityId};
  if (pair.second < pair.first)
    std::swap(pair.first, pair.second);
  return pair;
}
struct PairHash {
  std::size_t operator()(const Pair &pair) const {
    const auto first = std::hash<std::string_view>{}(pair.first);
    const auto second = std::hash<std::string_view>{}(pair.second);
    return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
  }
};

bool pairsSorted(const std::vector<PhysicsContact3D> &contacts,
                 bool skipExits) {
  Pair previous{};
  bool hasPrevious = false;
  for (const auto &contact : contacts) {
    if (skipExits && contact.phase == "exit")
      continue;
    const auto pair = key(contact);
    if (hasPrevious && pair < previous)
      return false;
    previous = pair;
    hasPrevious = true;
  }
  return true;
}

void assignSortedPhases(std::vector<PhysicsContact3D> &current,
                        const std::vector<PhysicsContact3D> &previous) {
  const auto currentCount = current.size();
  std::size_t oldIndex = 0;
  for (auto &contact : current) {
    const auto pair = key(contact);
    while (oldIndex < previous.size() && (previous[oldIndex].phase == "exit" ||
                                          key(previous[oldIndex]) < pair))
      ++oldIndex;
    contact.phase =
        oldIndex < previous.size() && key(previous[oldIndex]) == pair ? "stay"
                                                                      : "enter";
  }
  std::size_t newIndex = 0;
  for (const auto &contact : previous) {
    if (contact.phase == "exit")
      continue;
    const auto pair = key(contact);
    while (newIndex < currentCount && key(current[newIndex]) < pair)
      ++newIndex;
    if (newIndex < currentCount && key(current[newIndex]) == pair)
      continue;
    auto exited = contact;
    exited.phase = "exit";
    exited.penetration = 0.0F;
    current.push_back(std::move(exited));
  }
}
} // namespace

void assignContactPhases3D(std::vector<PhysicsContact3D> &current,
                           const std::vector<PhysicsContact3D> &previous) {
  // Views borrow contact strings. Reserve all possible exits before building
  // the sets so a push cannot relocate short strings and invalidate the keys.
  current.reserve(current.size() + previous.size());
  // Rigid contacts arrive pair-sorted, with previous exits appended afterward.
  // Merge those streams without allocating hash nodes. Character/mixed callers
  // may use another deterministic order and retain the general path below.
  if (pairsSorted(current, false) && pairsSorted(previous, true)) {
    assignSortedPhases(current, previous);
    return;
  }
  std::pmr::monotonic_buffer_resource scratch;
  std::pmr::unordered_set<Pair, PairHash> previousPairs{&scratch};
  std::pmr::unordered_set<Pair, PairHash> currentPairs{&scratch};
  previousPairs.reserve(previous.size());
  currentPairs.reserve(current.size());
  for (const auto &contact : previous)
    if (contact.phase != "exit")
      previousPairs.insert(key(contact));
  for (auto &contact : current) {
    const auto pair = key(contact);
    contact.phase = previousPairs.contains(pair) ? "stay" : "enter";
    currentPairs.insert(pair);
  }
  for (const auto &contact : previous) {
    if (contact.phase == "exit" || currentPairs.contains(key(contact)))
      continue;
    auto exited = contact;
    exited.phase = "exit";
    exited.penetration = 0.0F;
    current.push_back(std::move(exited));
  }
}
} // namespace demi::runtime
