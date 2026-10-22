#pragma once

#include "demi/runtime/scene/model/EntityCollection.h"
#include <algorithm>
#include <vector>

namespace demi::runtime {

// Query the world's typed pools, not every entity's type map. The returned
// indices preserve authored order for deterministic simulation. Structural
// mutation and query iteration must remain serialized by the caller.
class EntityComponentQuery {
public:
  template <typename... Components>
  [[nodiscard]] std::vector<std::size_t>
  queryAny(const EntityCollection &entities) const {
    static_assert(sizeof...(Components) > 0);
    std::vector<std::size_t> indices;
    const auto &registry = entities.componentRegistry().native();
    const auto append = [&]<typename Component> {
      for (const auto handle : registry.view<const Component>()) {
        if (const auto index = entities.indexOf(handle)) {
          indices.push_back(*index);
        }
      }
    };
    (append.template operator()<Components>(), ...);
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
  }
};

} // namespace demi::runtime
