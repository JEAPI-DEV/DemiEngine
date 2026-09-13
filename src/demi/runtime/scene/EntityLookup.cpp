#include "demi/runtime/scene/EntityLookup.h"
#include "demi/runtime/scene/model/Entity.h"
#include <algorithm>

namespace demi::runtime {
void EntityLookup::clear() {
  indices_.clear();
  size_ = 0;
}

void EntityLookup::rebuild(const std::vector<Entity> &entities) {
  indices_.clear();
  indices_.reserve(entities.size());
  for (std::size_t i = 0; i < entities.size(); ++i)
    indices_.try_emplace(entities[i].id, i);
  size_ = entities.size();
}

std::optional<std::size_t>
EntityLookup::index(const std::vector<Entity> &entities,
                    const std::string &id) {
  if (size_ != entities.size())
    rebuild(entities);
  auto found = indices_.find(id);
  if (found != indices_.end()) {
    if (found->second < entities.size() && entities[found->second].id == id)
      return found->second;
    // Reorder, replacement, or rename without a size change.
    rebuild(entities);
    found = indices_.find(id);
    return found == indices_.end() ? std::nullopt
                                   : std::make_optional(found->second);
  }
  // A same-sized collection can gain a different ID through replacement. Do
  // not cache misses: there is no mutation generation on the public vector.
  const auto match = std::ranges::find(entities, id, &Entity::id);
  if (match == entities.end())
    return std::nullopt;
  const auto position = static_cast<std::size_t>(match - entities.begin());
  rebuild(entities);
  return position;
}

Entity *EntityLookup::find(std::vector<Entity> &entities,
                           const std::string &id) {
  const auto position = index(entities, id);
  return position ? &entities[*position] : nullptr;
}

const Entity *EntityLookup::find(const std::vector<Entity> &entities,
                                 const std::string &id) {
  const auto position = index(entities, id);
  return position ? &entities[*position] : nullptr;
}
} // namespace demi::runtime
