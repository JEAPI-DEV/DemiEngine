#pragma once

#include <entt/core/any.hpp>
#include <entt/core/type_info.hpp>
#include <entt/entity/fwd.hpp>

#include <concepts>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace demi::runtime {

class ComponentRegistry;

// Construction and snapshot values have no live entity or sparse pool pages.
// Binding publishes these temporary values into the world's typed pools.
class DetachedComponents {
public:
  template <typename ComponentType> bool contains() const {
    return values_.contains(
        entt::type_hash<std::remove_cvref_t<ComponentType>>::value());
  }

  template <typename ComponentType>
  std::remove_reference_t<ComponentType> *get() {
    using StoredType = std::remove_cvref_t<ComponentType>;
    const auto found = values_.find(entt::type_hash<StoredType>::value());
    return found == values_.end()
               ? nullptr
               : entt::any_cast<StoredType>(&found->second.value);
  }

  template <typename ComponentType>
  const std::remove_cvref_t<ComponentType> *get() const {
    using StoredType = std::remove_cvref_t<ComponentType>;
    const auto found = values_.find(entt::type_hash<StoredType>::value());
    return found == values_.end()
               ? nullptr
               : entt::any_cast<StoredType>(&found->second.value);
  }

  template <typename ComponentType>
    requires std::copy_constructible<std::remove_cvref_t<ComponentType>>
  std::remove_cvref_t<ComponentType> &set(ComponentType &&component) {
    using StoredType = std::remove_cvref_t<ComponentType>;
    Entry entry{entt::any{std::in_place_type<StoredType>,
                          std::forward<ComponentType>(component)},
                &publish<StoredType>};
    auto result = values_.insert_or_assign(entt::type_hash<StoredType>::value(),
                                           std::move(entry));
    return *entt::any_cast<StoredType>(&result.first->second.value);
  }

  template <typename ComponentType> bool remove() {
    return values_.erase(
               entt::type_hash<std::remove_cvref_t<ComponentType>>::value()) !=
           0;
  }

  void clear() { values_.clear(); }

  void publishTo(ComponentRegistry &registry, entt::entity entity) const {
    for (const auto &[type, entry] : values_) {
      entry.publish(entry.value, registry, entity);
    }
  }

private:
  template <typename ComponentType>
  static void publish(const entt::any &value, ComponentRegistry &registry,
                      entt::entity entity);

  struct Entry {
    entt::any value;
    void (*publish)(const entt::any &, ComponentRegistry &, entt::entity);
  };
  std::unordered_map<entt::id_type, Entry> values_;
};

} // namespace demi::runtime
