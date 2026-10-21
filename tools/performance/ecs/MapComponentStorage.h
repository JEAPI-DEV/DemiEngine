#pragma once

// Historical production control for the ECS evaluation only. Never linked
// into the engine; retained so migration results remain reproducible.

#include <any>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace demi::evaluation {

class MapComponentStorage {
public:
  MapComponentStorage() = default;

  ~MapComponentStorage() {
    // Even an empty observed slot may be reconstructed in the same allocation.
    invalidateMembership();
  }

  MapComponentStorage(const MapComponentStorage &other)
      : components_(other.components_) {}

  MapComponentStorage(MapComponentStorage &&other) noexcept
      : components_(std::move(other.components_)) {
    other.invalidateMembership();
  }

  MapComponentStorage &operator=(const MapComponentStorage &other) {
    if (this != &other) {
      invalidateMembership();
      components_ = other.components_;
    }
    return *this;
  }

  MapComponentStorage &operator=(MapComponentStorage &&other) noexcept {
    if (this != &other) {
      invalidateMembership();
      other.invalidateMembership();
      components_ = std::move(other.components_);
    }
    return *this;
  }

  [[nodiscard]] static std::uint64_t membershipEpoch() noexcept {
    return membershipEpoch_.load(std::memory_order_relaxed);
  }

  // Replacing an existing std::any also invalidates borrowed component addresses,
  // even though the set of component types has not changed.
  [[nodiscard]] static std::uint64_t storageEpoch() noexcept {
    return storageEpoch_.load(std::memory_order_relaxed);
  }

  // Query scans observe every slot, including entities with no matching type.
  // Standalone parser/default-value temporaries do not invalidate live queries.
  void observeMembership() const noexcept { membershipObserved_ = true; }

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
    if (inserted) {
      invalidateMembership();
    } else if (membershipObserved_) {
      storageEpoch_.fetch_add(1, std::memory_order_relaxed);
    }
    return *std::any_cast<StoredType>(&iterator->second);
  }

  template <typename ComponentType> bool remove() {
    const bool removed = components_.erase(typeKey<ComponentType>()) != 0;
    if (removed) {
      invalidateMembership();
    }
    return removed;
  }

  void clear() {
    if (!components_.empty()) {
      components_.clear();
      invalidateMembership();
    }
  }

private:
  // Membership can change through public entity storage, vector replacement or
  // reordering. A shared epoch makes those mutations visible to world queries.
  void invalidateMembership() const noexcept {
    if (membershipObserved_) {
      membershipEpoch_.fetch_add(1, std::memory_order_relaxed);
      storageEpoch_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  mutable bool membershipObserved_ = false;
  inline static std::atomic<std::uint64_t> membershipEpoch_{1};
  inline static std::atomic<std::uint64_t> storageEpoch_{1};

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

} // namespace demi::evaluation
