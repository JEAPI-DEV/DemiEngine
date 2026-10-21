#pragma once

#include "demi/runtime/scene/model/ComponentRegistry.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace demi::runtime {

class ComponentStorage {
public:
  ComponentStorage() = default;

  ~ComponentStorage() { release(); }

  ComponentStorage(const ComponentStorage &other) {
    if (other.owner_ != nullptr) {
      detached_ = std::make_unique<DetachedComponents>();
      other.owner_->snapshotComponents(other.handle_, *detached_);
    } else if (other.detached_ != nullptr) {
      detached_ = std::make_unique<DetachedComponents>(*other.detached_);
    }
  }

  ComponentStorage(ComponentStorage &&other) noexcept
      : owner_(std::move(other.owner_)),
        handle_(std::exchange(other.handle_, entt::null)),
        detached_(std::move(other.detached_)) {}

  ComponentStorage &operator=(const ComponentStorage &other) {
    if (this != &other) {
      ComponentStorage copy(other);
      if (owner_ != nullptr) {
        copy.bind(owner_);
      }
      *this = std::move(copy);
    }
    return *this;
  }

  ComponentStorage &operator=(ComponentStorage &&other) noexcept {
    if (this != &other) {
      release();
      owner_ = std::move(other.owner_);
      handle_ = std::exchange(other.handle_, entt::null);
      detached_ = std::move(other.detached_);
    }
    return *this;
  }

  [[nodiscard]] const std::shared_ptr<ComponentRegistry> &
  owner() const noexcept {
    return owner_;
  }

  [[nodiscard]] entt::entity handle() const noexcept { return handle_; }

  // Build the destination row before releasing the source, so failed copies
  // cannot publish a partially migrated entity or lose its original values.
  void bind(std::shared_ptr<ComponentRegistry> destination) {
    if (destination == owner_) {
      return;
    }
    if (destination == nullptr) {
      ComponentStorage snapshot(*this);
      *this = std::move(snapshot);
      return;
    }
    const auto target = destination->native().create();
    try {
      if (owner_ != nullptr) {
        owner_->copyComponents(handle_, *destination, target);
      } else if (detached_ != nullptr) {
        detached_->publishTo(*destination, target);
      }
    } catch (...) {
      destination->native().destroy(target);
      throw;
    }
    release();
    owner_ = std::move(destination);
    handle_ = target;
  }

  template <typename ComponentType> [[nodiscard]] bool contains() const {
    using StoredType = std::remove_cvref_t<ComponentType>;
    return owner_ != nullptr
               ? owner_->native().all_of<StoredType>(handle_)
               : detached_ != nullptr && detached_->contains<StoredType>();
  }

  template <typename ComponentType>
  [[nodiscard]] std::remove_reference_t<ComponentType> *get() {
    using StoredType = std::remove_cvref_t<ComponentType>;
    if (owner_ != nullptr) {
      return owner_->native().try_get<StoredType>(handle_);
    }
    return detached_ == nullptr ? nullptr : detached_->get<StoredType>();
  }

  template <typename ComponentType>
  [[nodiscard]] const std::remove_cvref_t<ComponentType> *get() const {
    using StoredType = std::remove_cvref_t<ComponentType>;
    if (owner_ != nullptr) {
      return std::as_const(owner_->native()).try_get<StoredType>(handle_);
    }
    return detached_ == nullptr ? nullptr
                                : std::as_const(*detached_).get<StoredType>();
  }

  template <typename ComponentType>
    requires std::copy_constructible<std::remove_cvref_t<ComponentType>>
  std::remove_cvref_t<ComponentType> &set(ComponentType &&component) {
    using StoredType = std::remove_cvref_t<ComponentType>;
    // Capture first: callers may replace a component using its current value.
    StoredType replacement(std::forward<ComponentType>(component));
    if (owner_ == nullptr) {
      if (detached_ == nullptr) {
        detached_ = std::make_unique<DetachedComponents>();
      }
      return detached_->set(std::move(replacement));
    }
    owner_->registerType<StoredType>();
    const bool inserted = !contains<StoredType>();
    if (!inserted) {
      if constexpr (std::is_move_assignable_v<StoredType>) {
        return owner_->native().patch<StoredType>(
            handle_,
            [&](StoredType &existing) { existing = std::move(replacement); });
      } else {
        owner_->native().remove<StoredType>(handle_);
      }
    }
    return owner_->native().emplace<StoredType>(handle_,
                                                std::move(replacement));
  }

  template <typename ComponentType> bool remove() {
    using StoredType = std::remove_cvref_t<ComponentType>;
    return owner_ != nullptr
               ? owner_->native().remove<StoredType>(handle_) != 0
               : detached_ != nullptr && detached_->remove<StoredType>();
  }

  void clear() {
    if (owner_ == nullptr) {
      detached_.reset();
      return;
    }
    // Keep the row identity: collection indexes and registry views share it.
    for (auto &&[type, pool] : owner_->native().storage()) {
      pool.remove(handle_);
    }
  }

private:
  void release() noexcept {
    if (owner_ != nullptr && handle_ != entt::null) {
      owner_->native().destroy(handle_);
    }
    handle_ = entt::null;
    owner_.reset();
    detached_.reset();
  }

  std::shared_ptr<ComponentRegistry> owner_;
  entt::entity handle_ = entt::null;
  std::unique_ptr<DetachedComponents> detached_;
};

} // namespace demi::runtime
