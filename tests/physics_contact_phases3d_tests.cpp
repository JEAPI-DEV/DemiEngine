#include "demi/runtime/physics/PhysicsContactPhases3D.h"
#include <algorithm>
#include <iostream>
#include <unordered_set>

using namespace demi::runtime;

bool testContactPhaseBookkeeping(int ordering, int pairCount) {
  const auto pairKey = [](const PhysicsContact3D &contact) {
    return std::min(contact.entityId, contact.otherEntityId) + '\0' +
           std::max(contact.entityId, contact.otherEntityId);
  };
  std::vector<PhysicsContact3D> previous;
  for (int frame = 0; frame < 60; ++frame) {
    std::vector<PhysicsContact3D> current;
    for (int i = 0; i < pairCount; ++i) {
      if ((i + frame) % 5 == 0)
        continue;
      const std::string first = i % 2
                                    ? "a" + std::to_string(i)
                                    : std::string(40, 'a') + std::to_string(i);
      const std::string second = "b" + std::to_string(i);
      current.push_back({.entityId = first,
                         .otherEntityId = second,
                         .otherLayer = "solid",
                         .point = {1, 2, 3},
                         .normal = {0, 1, 0},
                         .penetration = 0.25F,
                         .isTrigger = i % 3 == 0});
      if (i % 2)
        current.push_back({.entityId = second, .otherEntityId = first});
    }
    if (frame % 15 == 0)
      current.clear();
    if (ordering == 1 || (ordering == 2 && frame % 2 == 0))
      std::ranges::sort(current, {}, pairKey);
    auto reference = current;
    std::unordered_set<std::string> oldPairs, newPairs;
    for (const auto &contact : previous)
      if (contact.phase != "exit")
        oldPairs.insert(pairKey(contact));
    for (auto &contact : reference) {
      contact.phase = oldPairs.contains(pairKey(contact)) ? "stay" : "enter";
      newPairs.insert(pairKey(contact));
    }
    for (auto contact : previous) {
      if (contact.phase == "exit" || newPairs.contains(pairKey(contact)))
        continue;
      contact.phase = "exit";
      contact.penetration = 0;
      reference.push_back(std::move(contact));
    }
    assignContactPhases3D(current, previous);
    if (current.size() != reference.size())
      return false;
    for (std::size_t i = 0; i < current.size(); ++i) {
      const auto &a = current[i];
      const auto &b = reference[i];
      if (a.entityId != b.entityId || a.otherEntityId != b.otherEntityId ||
          a.phase != b.phase || a.otherLayer != b.otherLayer ||
          a.isTrigger != b.isTrigger || a.penetration != b.penetration ||
          a.point.x != b.point.x || a.point.y != b.point.y ||
          a.point.z != b.point.z || a.normal.x != b.normal.x ||
          a.normal.y != b.normal.y || a.normal.z != b.normal.z)
        return false;
    }
    previous = std::move(current);
  }
  return true;
}

int main() {
  for (int pairCount : {0, 1, 80, 4000})
    for (int ordering : {0, 1, 2})
      if (!testContactPhaseBookkeeping(ordering, pairCount)) {
        std::cerr << "Contact phase data/order differs from reference "
                     "implementation\n";
        return 1;
      }
  return 0;
}
