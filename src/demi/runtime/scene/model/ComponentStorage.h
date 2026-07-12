#pragma once

#include <any>
#include <concepts>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace demi::runtime {

class ComponentStorage {
public:
  template <typename ComponentType>
  [[nodiscard]] bool contains() const {
    return components_.contains(typeKey<ComponentType>());
  }

  template <typename ComponentType>
  [[nodiscard]] ComponentType *get() {
    const auto iterator = components_.find(typeKey<ComponentType>());
    return iterator == components_.end()
               ? nullptr
               : std::any_cast<ComponentType>(&iterator->second);
  }

  template <typename ComponentType>
  [[nodiscard]] const ComponentType *get() const {
    const auto iterator = components_.find(typeKey<ComponentType>());
    return iterator == components_.end()
               ? nullptr
               : std::any_cast<ComponentType>(&iterator->second);
  }

  template <typename ComponentType>
    requires std::copy_constructible<std::remove_cvref_t<ComponentType>>
  std::remove_cvref_t<ComponentType> &set(ComponentType &&component) {
    using StoredType = std::remove_cvref_t<ComponentType>;
    auto [iterator, inserted] = components_.insert_or_assign(
        typeKey<StoredType>(), StoredType(std::forward<ComponentType>(component)));
    (void)inserted;
    return *std::any_cast<StoredType>(&iterator->second);
  }

  template <typename ComponentType> bool remove() {
    return components_.erase(typeKey<ComponentType>()) != 0;
  }

  void clear() { components_.clear(); }

private:
  template <typename ComponentType>
  [[nodiscard]] static std::type_index typeKey() {
    return std::type_index(typeid(std::remove_cvref_t<ComponentType>));
  }

  std::unordered_map<std::type_index, std::any> components_;
};

} // namespace demi::runtime
