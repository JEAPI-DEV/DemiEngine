#include "demi/runtime/scene/WorldTransformSnapshot3D.h"

#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/model/World.h"

namespace demi::runtime {
namespace {

bool sameVector(Vec3 left, Vec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool sameLocal(const Transform3DComponent &left,
               const Transform3DComponent &right) {
  return sameVector(left.position, right.position) &&
         sameVector(left.rotation, right.rotation) &&
         sameVector(left.scale, right.scale) && left.parent == right.parent;
}

} // namespace

const std::vector<std::optional<WorldTransform3D>> &
WorldTransformSnapshot3D::refresh(const World &world, JobSystem *jobs) {
  ProfileScope scope("Renderer3D.world_transforms");
  const auto count = world.entities.size();
  const auto &registry = world.entities.componentRegistry();
  const auto storageRevision = registry.storageRevision();
  if (orderRevision_ != world.entities.orderRevision() ||
      entityData_ != world.entities.data() || localSources_.size() != count ||
      storageRevision_ != storageRevision ||
      registryIdentity_ != registry.identity()) {
    localSources_.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
      const auto &entity = world.entities[index];
      localSources_[index] = entity.component<Transform3DComponent>();
    }
    entityData_ = world.entities.data();
    storageRevision_ = storageRevision;
    registryIdentity_ = registry.identity();
    orderRevision_ = world.entities.orderRevision();
  }
  bool idsChanged = ids_.size() != count;
  bool localsChanged = locals_.size() != count;
  bool parentsChanged = localsChanged;
  ids_.resize(count);
  locals_.resize(count);
  changes_.assign(count, {});
  const auto inspect = [&](std::size_t index) {
    auto &changed = changes_[index];
    const auto &entity = world.entities[index];
    if (ids_[index] != entity.id) {
      ids_[index] = entity.id;
      changed.id = true;
    }
    const auto *local = localSources_[index];
    auto &previous = locals_[index];
    if (local == nullptr) {
      if (previous) {
        previous.reset();
        changed.local = true;
        changed.parent = true;
      }
      return;
    }
    if (!previous || !sameLocal(*previous, *local)) {
      if (!previous || previous->parent != local->parent) {
        changed.parent = true;
      }
      previous = *local;
      changed.local = true;
    }
  };
  // Only independent content checks run on workers. Hierarchy resolution and
  // publication stay ordered on the caller after every check has completed.
  if (jobs && count >= 1024) {
    jobs->parallelFor(count, 256, inspect);
  } else {
    for (std::size_t index = 0; index < count; ++index) {
      inspect(index);
    }
  }
  for (const auto &changed : changes_) {
    idsChanged |= changed.id;
    localsChanged |= changed.local;
    parentsChanged |= changed.parent;
  }
  if (idsChanged) {
    rebuildIndices();
  }
  if (idsChanged || parentsChanged) {
    rebuildParents();
  }
  if (idsChanged || localsChanged) {
    resolve();
  }
  return results_;
}

void WorldTransformSnapshot3D::rebuildIndices() {
  indices_.clear();
  indices_.reserve(ids_.size());
  for (std::size_t index = 0; index < ids_.size(); ++index) {
    // Match the individual resolver's first matching entity for duplicate IDs.
    indices_.try_emplace(ids_[index], index);
  }
}

void WorldTransformSnapshot3D::rebuildParents() {
  parents_.assign(locals_.size(), std::nullopt);
  for (std::size_t index = 0; index < locals_.size(); ++index) {
    const auto &local = locals_[index];
    if (!local || local->parent.empty()) {
      continue;
    }
    const auto parent = indices_.find(local->parent);
    if (parent != indices_.end()) {
      parents_[index] = parent->second;
    }
  }
}

void WorldTransformSnapshot3D::resolve() {
  results_.assign(locals_.size(), std::nullopt);
  // 0 = unseen, 1 = current ancestor chain, 2 = resolved (possibly invalid).
  states_.assign(locals_.size(), 0);
  for (std::size_t index = 0; index < locals_.size(); ++index) {
    if (states_[index] == 2) {
      continue;
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
      const auto &local = locals_[*entry];
      if (valid && local) {
        WorldTransform3D transform{.position = local->position,
                                   .rotation = local->rotation,
                                   .scale = local->scale};
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
}

std::vector<std::optional<WorldTransform3D>>
resolveWorldTransforms3D(const World &world) {
  WorldTransformSnapshot3D snapshot;
  return snapshot.refresh(world);
}

} // namespace demi::runtime
