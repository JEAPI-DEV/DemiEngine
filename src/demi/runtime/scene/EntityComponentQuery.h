#pragma once

#include "demi/runtime/scene/model/Entity.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

// Ordered membership only: callers fetch current components and enabled state.
// No entity or component pointer survives collection mutation. Queries and
// structural mutations of their entity collection must run serially; the epoch
// detects changes between queries, not concurrent mutation during iteration.
class EntityComponentQuery {
public:
  EntityComponentQuery() = default;
  EntityComponentQuery(const EntityComponentQuery &) {}
  EntityComponentQuery(EntityComponentQuery &&) noexcept {}

  EntityComponentQuery &operator=(const EntityComponentQuery &) {
    entries_.clear();
    return *this;
  }

  EntityComponentQuery &operator=(EntityComponentQuery &&) noexcept {
    entries_.clear();
    return *this;
  }

  template <typename... Components>
  [[nodiscard]] const std::vector<std::size_t> &
  queryAny(const std::vector<Entity> &entities) {
    static_assert(sizeof...(Components) > 0);
    static const char token = 0;
    auto &entry = entries_[&token];
    const auto epoch = ComponentStorage::membershipEpoch();
    if (entry.epoch == epoch && entry.data == entities.data() &&
        entry.size == entities.size()) {
      return entry.indices;
    }
    entry.indices.clear();
    for (std::size_t index = 0; index < entities.size(); ++index) {
      entities[index].components.observeMembership();
      if ((entities[index].hasComponent<Components>() || ...)) {
        entry.indices.push_back(index);
      }
    }
    entry.data = entities.data();
    entry.size = entities.size();
    entry.epoch = epoch;
    return entry.indices;
  }

private:
  struct Entry {
    const Entity *data = nullptr;
    std::size_t size = 0;
    std::uint64_t epoch = 0;
    std::vector<std::size_t> indices;
  };

  std::unordered_map<const void *, Entry> entries_;
};

} // namespace demi::runtime
