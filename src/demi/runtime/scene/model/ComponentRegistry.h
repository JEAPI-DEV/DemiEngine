#pragma once

#include "demi/runtime/scene/model/DetachedComponents.h"

#include <entt/entity/registry.hpp>

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <unordered_map>

namespace demi::runtime {

// One registry owns the typed component pools for an entire entity collection.
// Copy dispatch is registered by set<T>, including types outside engine
// metadata.
class ComponentRegistry {
public:
  ComponentRegistry() = default;
  ComponentRegistry(const ComponentRegistry &) = delete;
  ComponentRegistry &operator=(const ComponentRegistry &) = delete;
  ComponentRegistry(ComponentRegistry &&) = delete;
  ComponentRegistry &operator=(ComponentRegistry &&) = delete;

  ~ComponentRegistry() {
    // Destruction signals still reference this owner while rows are released.
    registry_.clear();
  }

  [[nodiscard]] std::uint64_t identity() const noexcept { return identity_; }
  // Tracks pool membership and published updates. Borrowed mutable component
  // references do not publish updates; value-sensitive consumers compare
  // values.
  [[nodiscard]] std::uint64_t storageRevision() const noexcept {
    return storageRevision_;
  }

  [[nodiscard]] entt::registry &native() noexcept { return registry_; }
  [[nodiscard]] const entt::registry &native() const noexcept {
    return registry_;
  }

  template <typename ComponentType> void registerType() {
    using StoredType = std::remove_cvref_t<ComponentType>;
    const auto registration = copiers_.try_emplace(
        entt::type_hash<StoredType>::value(),
        Operations{&copyComponent<StoredType>, &snapshotComponent<StoredType>});
    if (registration.second) {
      registry_.on_construct<StoredType>()
          .template connect<&ComponentRegistry::storageChanged>(*this);
      registry_.on_destroy<StoredType>()
          .template connect<&ComponentRegistry::storageChanged>(*this);
      registry_.on_update<StoredType>()
          .template connect<&ComponentRegistry::storageChanged>(*this);
    }
  }

  void copyComponents(entt::entity source, ComponentRegistry &destination,
                      entt::entity target) const {
    for (const auto &[type, operations] : copiers_) {
      operations.copy(*this, source, destination, target);
    }
  }

  void snapshotComponents(entt::entity source,
                          DetachedComponents &values) const {
    for (const auto &[type, operations] : copiers_) {
      operations.snapshot(*this, source, values);
    }
  }

private:
  using Copier = void (*)(const ComponentRegistry &, entt::entity,
                          ComponentRegistry &, entt::entity);

  struct Operations {
    Copier copy;
    void (*snapshot)(const ComponentRegistry &, entt::entity,
                     DetachedComponents &);
  };

  template <typename ComponentType>
  static void snapshotComponent(const ComponentRegistry &source,
                                entt::entity entity,
                                DetachedComponents &values) {
    if (const auto *component =
            source.native().try_get<ComponentType>(entity)) {
      values.set(*component);
    }
  }

  template <typename ComponentType>
  static void
  copyComponent(const ComponentRegistry &source, entt::entity sourceEntity,
                ComponentRegistry &destination, entt::entity targetEntity) {
    const auto *component =
        source.native().try_get<ComponentType>(sourceEntity);
    if (component != nullptr) {
      destination.registerType<ComponentType>();
      destination.native().emplace<ComponentType>(targetEntity, *component);
    }
  }

  void storageChanged(entt::registry &, entt::entity) noexcept {
    ++storageRevision_;
  }

  inline static std::atomic<std::uint64_t> nextIdentity_{1};
  const std::uint64_t identity_ =
      nextIdentity_.fetch_add(1, std::memory_order_relaxed);
  std::uint64_t storageRevision_ = 0;
  entt::registry registry_;
  std::unordered_map<entt::id_type, Operations> copiers_;
};

template <typename ComponentType>
void DetachedComponents::publish(const entt::any &value,
                                 ComponentRegistry &registry,
                                 entt::entity entity) {
  registry.registerType<ComponentType>();
  registry.native().emplace<ComponentType>(
      entity, entt::any_cast<const ComponentType &>(value));
}

} // namespace demi::runtime
