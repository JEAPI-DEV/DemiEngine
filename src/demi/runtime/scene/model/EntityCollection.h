#pragma once

#include "demi/runtime/scene/model/ComponentRegistry.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <vector>

namespace demi::runtime {

// Authored order and component ownership travel together. Detached parser and
// prefab values are migrated into the collection's single component registry.
class EntityCollection {
public:
  using value_type = Entity;
  using iterator = std::vector<Entity>::iterator;
  using const_iterator = std::vector<Entity>::const_iterator;
  using reverse_iterator = std::vector<Entity>::reverse_iterator;
  using const_reverse_iterator = std::vector<Entity>::const_reverse_iterator;
  using size_type = std::size_t;

  EntityCollection();
  explicit EntityCollection(size_type size);
  EntityCollection(std::initializer_list<Entity> entities);
  EntityCollection(const EntityCollection &other);
  EntityCollection(EntityCollection &&other) noexcept;
  EntityCollection &operator=(const EntityCollection &other);
  EntityCollection &operator=(EntityCollection &&other) noexcept;
  EntityCollection &operator=(std::vector<Entity> entities);
  EntityCollection &operator=(std::initializer_list<Entity> entities);

  [[nodiscard]] std::uint64_t orderRevision() const noexcept {
    return orderRevision_;
  }
  [[nodiscard]] bool empty() const noexcept { return entities_.empty(); }
  [[nodiscard]] size_type size() const noexcept { return entities_.size(); }
  [[nodiscard]] size_type max_size() const noexcept {
    return entities_.max_size();
  }
  [[nodiscard]] size_type capacity() const noexcept {
    return entities_.capacity();
  }
  Entity *data() noexcept { return entities_.data(); }
  const Entity *data() const noexcept { return entities_.data(); }
  iterator begin() noexcept { return entities_.begin(); }
  const_iterator begin() const noexcept { return entities_.begin(); }
  const_iterator cbegin() const noexcept { return entities_.cbegin(); }
  iterator end() noexcept { return entities_.end(); }
  const_iterator end() const noexcept { return entities_.end(); }
  const_iterator cend() const noexcept { return entities_.cend(); }
  reverse_iterator rbegin() noexcept { return entities_.rbegin(); }
  const_reverse_iterator rbegin() const noexcept { return entities_.rbegin(); }
  reverse_iterator rend() noexcept { return entities_.rend(); }
  const_reverse_iterator rend() const noexcept { return entities_.rend(); }
  Entity &operator[](size_type index) { return entities_[index]; }
  const Entity &operator[](size_type index) const { return entities_[index]; }
  Entity &at(size_type index) { return entities_.at(index); }
  const Entity &at(size_type index) const { return entities_.at(index); }
  Entity &front() { return entities_.front(); }
  const Entity &front() const { return entities_.front(); }
  Entity &back() { return entities_.back(); }
  const Entity &back() const { return entities_.back(); }

  void reserve(size_type capacity) { entities_.reserve(capacity); }
  void clear();
  void resize(size_type size);
  void pop_back();
  // Stages registry ownership and order-index capacity without publishing a
  // row. Transactional callers perform this before changing any visible state.
  void prepareAdoption(Entity &entity);
  void push_back(Entity entity);
  template <typename... Arguments>
  Entity &emplace_back(Arguments &&...arguments) {
    push_back(Entity(std::forward<Arguments>(arguments)...));
    return back();
  }
  iterator insert(const_iterator position, Entity entity);
  template <typename InputIterator>
  iterator insert(const_iterator position, InputIterator first,
                  InputIterator last) {
    const auto offset = position - entities_.cbegin();
    std::vector<Entity> incoming(first, last);
    for (auto &entity : incoming) {
      prepareAdoption(entity);
    }
    auto result = entities_.insert(entities_.begin() + offset,
                                   std::make_move_iterator(incoming.begin()),
                                   std::make_move_iterator(incoming.end()));
    rebuildIndices();
    return result;
  }
  iterator erase(const_iterator position);
  iterator erase(const_iterator first, const_iterator last);
  template <typename Predicate> size_type eraseIf(Predicate predicate) {
    const auto removed = std::erase_if(entities_, std::move(predicate));
    if (removed != 0) {
      rebuildIndices();
    }
    return removed;
  }
  void replace(size_type index, Entity entity);
  void swapElements(size_type first, size_type second);
  void swap(EntityCollection &other) noexcept;

  const std::shared_ptr<ComponentRegistry> &registryOwner() const { return registry(); }

  ComponentRegistry &componentRegistry() { return *registry(); }
  const ComponentRegistry &componentRegistry() const { return *registry(); }
  [[nodiscard]] std::optional<size_type> indexOf(entt::entity handle) const;

private:
  const std::shared_ptr<ComponentRegistry> &registry() const;
  void rebuildIndices();

  mutable std::shared_ptr<ComponentRegistry> registry_;
  std::vector<Entity> entities_;
  struct OrderEntry {
    entt::entity handle = entt::null;
    size_type index = 0;
  };
  std::vector<OrderEntry> indices_;
  std::uint64_t orderRevision_ = 0;
};

} // namespace demi::runtime
