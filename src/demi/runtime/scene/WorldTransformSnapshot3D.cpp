#include "demi/runtime/scene/WorldTransformSnapshot3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/model/World.h"
#include <entt/entity/mixin.hpp>
#include <entt/entity/storage.hpp>

namespace demi::runtime {
struct WorldTransformSnapshot3D::ChangeTracker {
  explicit ChangeTracker(const std::shared_ptr<ComponentRegistry> &registry)
      : owner(registry) {
    registry->registerType<Transform3DComponent>();
    pending.bind(registry->native());
    pending.on_construct<Transform3DComponent>()
        .on_update<Transform3DComponent>()
        .on_destroy<Transform3DComponent>()
        .on_destroy<entt::entity, &ChangeTracker::forget>();
  }

  ~ChangeTracker() {
    // The world may die before its renderer. EnTT connections may only be
    // released while the signal owner is still alive.
    if (const auto alive = owner.lock()) {
      pending.reset();
    }
  }

  using Pending = entt::reactive_mixin<entt::storage<void>>;
  static void forget(Pending &pending, const entt::registry &,
                     entt::entity entity) {
    // Destruction changes authored order, which forces a full rebuild. Remove
    // the old generation before EnTT can recycle its sparse-set slot.
    pending.remove(entity);
  }

  std::weak_ptr<ComponentRegistry> owner;
  entt::reactive_mixin<entt::storage<void>> pending;
};

WorldTransformSnapshot3D::WorldTransformSnapshot3D() = default;
WorldTransformSnapshot3D::~WorldTransformSnapshot3D() = default;
WorldTransformSnapshot3D::WorldTransformSnapshot3D(
    WorldTransformSnapshot3D &&) noexcept = default;
WorldTransformSnapshot3D &WorldTransformSnapshot3D::operator=(
    WorldTransformSnapshot3D &&) noexcept = default;

const std::vector<std::optional<WorldTransform3D>> &
WorldTransformSnapshot3D::refresh(const World &world) {
  ProfileScope scope("Renderer3D.world_transforms");
  const auto &registry = world.entities.componentRegistry();
  const auto count = world.entities.size();
  const bool newRegistry =
      !changes_ || registryIdentity_ != registry.identity();
  const bool reordered =
      newRegistry || orderRevision_ != world.entities.orderRevision();
  if (newRegistry) {
    changes_ = std::make_unique<ChangeTracker>(world.entities.registryOwner());
  }

  dirty_.clear();
  bool idsChanged = reordered || ids_.size() != count;
  bool parentsChanged = reordered;
  ids_.resize(count);
  locals_.resize(count);
  // IDs are still editable native metadata. Preserve rename detection until
  // their mutation boundary is also explicit; component values are not polled.
  for (std::size_t index = 0; index < count; ++index) {
    const auto &entity = world.entities[index];
    if (ids_[index] != entity.id) {
      ids_[index] = entity.id;
      idsChanged = true;
    }
    if (reordered) {
      const auto *local = entity.component<Transform3DComponent>();
      locals_[index] = local ? std::optional(*local) : std::nullopt;
    }
  }

  if (!reordered) {
    for (const auto handle : changes_->pending) {
      const auto index = world.entities.indexOf(handle);
      if (!index) {
        continue;
      }
      const auto *local =
          world.entities[*index].component<Transform3DComponent>();
      auto &previous = locals_[*index];
      if (local && previous && *local == *previous) {
        continue;
      }
      if (!local && !previous) {
        continue;
      }
      parentsChanged |=
          !local || !previous || local->parent != previous->parent;
      previous = local ? std::optional(*local) : std::nullopt;
      dirty_.push_back(*index);
    }
  }

  if (idsChanged) {
    rebuildIndices();
  }
  if (idsChanged || parentsChanged) {
    rebuildParents();
    resolveAll();
  } else {
    resolveChanged();
  }
  changes_->pending.clear();
  registryIdentity_ = registry.identity();
  orderRevision_ = world.entities.orderRevision();
  RuntimeProfiler::setGauge("Renderer3D.transform_updates", dirty_.size());
  RuntimeProfiler::setGauge("Renderer3D.transforms_recomputed",
                            affected_.size());
  return results_;
}

void WorldTransformSnapshot3D::rebuildIndices() {
  indices_.clear();
  indices_.reserve(ids_.size());
  for (std::size_t index = 0; index < ids_.size(); ++index) {
    // Match the individual resolver's first entry for duplicate authored IDs.
    indices_.try_emplace(ids_[index], index);
  }
}

void WorldTransformSnapshot3D::rebuildParents() {
  parents_.assign(locals_.size(), std::nullopt);
  children_.clear();
  children_.resize(locals_.size());
  for (std::size_t index = 0; index < locals_.size(); ++index) {
    const auto &local = locals_[index];
    if (!local || local->parent.empty()) {
      continue;
    }
    const auto parent = indices_.find(local->parent);
    if (parent != indices_.end()) {
      parents_[index] = parent->second;
      children_[parent->second].push_back(index);
    }
  }
}

void WorldTransformSnapshot3D::resolveAll() {
  results_.assign(locals_.size(), std::nullopt);
  states_.assign(locals_.size(), 0);
  affected_.clear();
  for (std::size_t index = 0; index < locals_.size(); ++index) {
    affected_.push_back(index);
    resolveChain(index);
  }
}

void WorldTransformSnapshot3D::resolveChanged() {
  affected_.clear();
  const auto invalidate = [&](std::size_t index) {
    if (states_[index] != 0) {
      states_[index] = 0;
      affected_.push_back(index);
    }
  };
  for (const auto index : dirty_) {
    invalidate(index);
  }
  for (std::size_t cursor = 0; cursor < affected_.size(); ++cursor) {
    for (const auto child : children_[affected_[cursor]]) {
      invalidate(child);
    }
  }
  for (const auto index : affected_) {
    resolveChain(index);
  }
}

void WorldTransformSnapshot3D::resolveChain(std::size_t index) {
  if (states_[index] == 2) {
    return;
  }
  chain_.clear();
  auto current = index;
  bool valid = true;
  while (states_[current] != 2) {
    if (states_[current] == 1) {
      valid = false;
      break;
    }
    states_[current] = 1;
    chain_.push_back(current);
    const auto &local = locals_[current];
    if (!local) {
      valid = false;
      break;
    }
    if (local->parent.empty()) {
      break;
    }
    if (!parents_[current]) {
      valid = false;
      break;
    }
    current = *parents_[current];
  }
  for (auto entry = chain_.rbegin(); entry != chain_.rend(); ++entry) {
    results_[*entry].reset();
    const auto &local = locals_[*entry];
    if (valid && local) {
      WorldTransform3D transform{local->position, local->rotation,
                                 local->scale};
      if (!local->parent.empty()) {
        const auto &parent = results_[*parents_[*entry]];
        if (parent) {
          transform = composeWorldTransform3D(*parent, transform);
        } else {
          valid = false;
        }
      }
      if (valid) {
        results_[*entry] = transform;
      }
    }
    states_[*entry] = 2;
  }
}

std::vector<std::optional<WorldTransform3D>>
resolveWorldTransforms3D(const World &world) {
  WorldTransformSnapshot3D snapshot;
  return snapshot.refresh(world);
}
} // namespace demi::runtime
