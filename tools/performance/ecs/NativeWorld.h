#pragma once

#include "Workload.h"
#include "MapComponentStorage.h"
#include <stdexcept>

namespace demi::evaluation {
// Control uses the current production ComponentStorage, not its historical
// uncached RTTI implementation. Both controls expose the same mutation
// boundary.
class NativeWorld {
public:
  explicit NativeWorld(std::size_t count)
      : entities_(count), records_(count), dirty_(count, false) {
    for (std::size_t index = 0; index < count; ++index) {
      auto &entity = entities_[index];
      entity.set(Identity{"entity/" + std::to_string(index), index});
      entity.set(initialTransform(index));
      if (index % 10 != 0) {
        entity.set(MeshRendererComponent{});
      }
      markDirty(index);
    }
  }

  void move(std::size_t index) {
    entities_.at(index).get<Transform3DComponent>()->position.x += .25F;
    markDirty(index);
  }

  void toggleMesh(std::size_t index) {
    auto &entity = entities_.at(index);
    if (!entity.remove<MeshRendererComponent>()) {
      entity.set(MeshRendererComponent{});
    }
    markDirty(index);
  }

  void recreate(std::size_t index) {
    auto &entity = entities_.at(index);
    entity.clear();
    entity.set(Identity{"replacement/" + std::to_string(index), index});
    entity.set(initialTransform(index));
    entity.set(MeshRendererComponent{});
    markDirty(index);
  }

  std::size_t prepare(bool tracked) {
    const auto refresh = [&](std::size_t index) {
      const auto &entity = entities_[index];
      records_[index] = project(entity.get<Transform3DComponent>(),
                                entity.get<MeshRendererComponent>());
    };
    const auto visited = tracked ? pending_.size() : entities_.size();
    if (tracked) {
      for (const auto index : pending_) {
        refresh(index);
      }
    } else {
      for (std::size_t index = 0; index < entities_.size(); ++index) {
        refresh(index);
      }
    }
    for (const auto index : pending_) {
      dirty_[index] = false;
    }
    pending_.clear();
    return visited;
  }

  const std::vector<RenderRecord> &records() const { return records_; }

private:
  void markDirty(std::size_t index) {
    if (!dirty_[index]) {
      dirty_[index] = true;
      pending_.push_back(index);
    }
  }

  std::vector<MapComponentStorage> entities_;
  std::vector<RenderRecord> records_;
  std::vector<bool> dirty_;
  std::vector<std::size_t> pending_;
};
} // namespace demi::evaluation
