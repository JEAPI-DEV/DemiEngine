#pragma once

#include <any>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace demi::runtime {

class ComponentStorage {
public:
  template <typename ComponentType> [[nodiscard]] bool contains() const {
    return components_.contains(typeKey<ComponentType>());
  }

  template <typename ComponentType> [[nodiscard]] ComponentType *get() {
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
        typeKey<StoredType>(),
        StoredType(std::forward<ComponentType>(component)));
    (void)inserted;
    return *std::any_cast<StoredType>(&iterator->second);
  }

  template <typename ComponentType> bool remove() {
    return components_.erase(typeKey<ComponentType>()) != 0;
  }

  void clear() { components_.clear(); }

private:
  struct TypeKey {
    explicit TypeKey(std::type_index componentType)
        : type(componentType), hash(componentType.hash_code()) {}

    bool operator==(const TypeKey &other) const noexcept {
      return type == other.type;
    }

    std::type_index type;
    std::size_t hash;
  };

  struct TypeKeyHash {
    std::size_t operator()(const TypeKey &key) const noexcept {
      return key.hash;
    }
  };

  template <typename ComponentType>
  [[nodiscard]] static const TypeKey &typeKey() {
    using StoredType = std::remove_cvref_t<ComponentType>;
    // RTTI hash_code may hash the full type name on every call. Retain the
    // hash with the key while keeping type_index equality for collisions and
    // std::any_cast for checked value access.
    static const TypeKey key{std::type_index(typeid(StoredType))};
    return key;
  }

  std::unordered_map<TypeKey, std::any, TypeKeyHash> components_;
};

} // namespace demi::runtime
