#include "demi/runtime/scene/model/EntityCollection.h"

namespace demi::runtime {

EntityCollection::EntityCollection()
    : registry_(std::make_shared<ComponentRegistry>()) {}

EntityCollection::EntityCollection(size_type size) : EntityCollection() {
  resize(size);
}

EntityCollection::EntityCollection(std::initializer_list<Entity> entities)
    : EntityCollection() {
  insert(end(), entities.begin(), entities.end());
}

EntityCollection::EntityCollection(const EntityCollection &other)
    : EntityCollection() {
  insert(end(), other.begin(), other.end());
}

EntityCollection::EntityCollection(EntityCollection &&other) noexcept = default;

EntityCollection &EntityCollection::operator=(const EntityCollection &other) {
  if (this != &other) {
    EntityCollection snapshot(other);
    swap(snapshot);
  }
  return *this;
}

EntityCollection &
EntityCollection::operator=(EntityCollection &&other) noexcept {
  if (this != &other) {
    clear();
    registry_ = std::move(other.registry_);
    entities_ = std::move(other.entities_);
    indices_ = std::move(other.indices_);
  }
  return *this;
}

EntityCollection &EntityCollection::operator=(std::vector<Entity> entities) {
  EntityCollection replacement;
  replacement.insert(replacement.end(),
                     std::make_move_iterator(entities.begin()),
                     std::make_move_iterator(entities.end()));
  swap(replacement);
  return *this;
}

EntityCollection &
EntityCollection::operator=(std::initializer_list<Entity> entities) {
  EntityCollection replacement(entities);
  swap(replacement);
  return *this;
}

const std::shared_ptr<ComponentRegistry> &EntityCollection::registry() const {
  // A moved-from collection remains reusable without allocating during moves.
  if (!registry_) {
    registry_ = std::make_shared<ComponentRegistry>();
  }
  return registry_;
}

void EntityCollection::rebuildIndices() {
  ++orderRevision_;
  std::fill(indices_.begin(), indices_.end(), OrderEntry{});
  for (size_type index = 0; index < entities_.size(); ++index) {
    const auto handle = entities_[index].components.handle();
    indices_[entt::to_entity(handle)] = {handle, index};
  }
}

std::optional<EntityCollection::size_type>
EntityCollection::indexOf(entt::entity handle) const {
  const auto slot = entt::to_entity(handle);
  if (handle == entt::null || slot >= indices_.size()) {
    return std::nullopt;
  }
  const auto &entry = indices_[slot];
  if (entry.handle != handle || entry.index >= entities_.size() ||
      entities_[entry.index].components.handle() != handle) {
    return std::nullopt;
  }
  return entry.index;
}

void EntityCollection::prepareAdoption(Entity &entity) {
  entity.components.bind(registry());
  const auto slot =
      static_cast<size_type>(entt::to_entity(entity.components.handle()));
  if (indices_.size() <= slot) {
    indices_.resize(slot + 1);
  }
}

void EntityCollection::clear() {
  ++orderRevision_;
  entities_.clear();
  std::fill(indices_.begin(), indices_.end(), OrderEntry{});
}

void EntityCollection::resize(size_type size) {
  if (size <= entities_.size()) {
    entities_.resize(size);
    rebuildIndices();
    return;
  }
  std::vector<Entity> additions(size - entities_.size());
  for (auto &entity : additions) {
    prepareAdoption(entity);
  }
  entities_.reserve(size);
  entities_.insert(entities_.end(), std::make_move_iterator(additions.begin()),
                   std::make_move_iterator(additions.end()));
  rebuildIndices();
}

void EntityCollection::pop_back() {
  ++orderRevision_;
  indices_[entt::to_entity(entities_.back().components.handle())] = {};
  entities_.pop_back();
}

void EntityCollection::push_back(Entity entity) {
  prepareAdoption(entity);
  const auto handle = entity.components.handle();
  entities_.push_back(std::move(entity));
  indices_[entt::to_entity(handle)] = {handle, entities_.size() - 1};
  ++orderRevision_;
}

EntityCollection::iterator EntityCollection::insert(const_iterator position,
                                                    Entity entity) {
  prepareAdoption(entity);
  auto result = entities_.insert(position, std::move(entity));
  rebuildIndices();
  return result;
}

EntityCollection::iterator EntityCollection::erase(const_iterator position) {
  return erase(position, position + 1);
}

EntityCollection::iterator EntityCollection::erase(const_iterator first,
                                                   const_iterator last) {
  auto result = entities_.erase(first, last);
  rebuildIndices();
  return result;
}

void EntityCollection::replace(size_type index, Entity entity) {
  prepareAdoption(entity);
  const auto previousHandle = entities_.at(index).components.handle();
  entities_.at(index) = std::move(entity);
  indices_[entt::to_entity(previousHandle)] = {};
  const auto handle = entities_[index].components.handle();
  indices_[entt::to_entity(handle)] = {handle, index};
  ++orderRevision_;
}

void EntityCollection::swapElements(size_type first, size_type second) {
  std::swap(entities_.at(first), entities_.at(second));
  const auto firstHandle = entities_[first].components.handle();
  const auto secondHandle = entities_[second].components.handle();
  indices_[entt::to_entity(firstHandle)] = {firstHandle, first};
  indices_[entt::to_entity(secondHandle)] = {secondHandle, second};
  ++orderRevision_;
}

void EntityCollection::swap(EntityCollection &other) noexcept {
  ++orderRevision_;
  ++other.orderRevision_;
  registry_.swap(other.registry_);
  entities_.swap(other.entities_);
  indices_.swap(other.indices_);
}

} // namespace demi::runtime
