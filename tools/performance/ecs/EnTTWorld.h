#pragma once

#include "Workload.h"
#include <entt/entity/registry.hpp>

namespace demi::evaluation {
class EnTTWorld {
  struct Dirty {};

public:
  explicit EnTTWorld(std::size_t count) : handles_(count), records_(count) {
    registry_.on_construct<Transform3DComponent>().connect<&EnTTWorld::changed>(
        *this);
    registry_.on_update<Transform3DComponent>().connect<&EnTTWorld::changed>(
        *this);
    registry_.on_construct<MeshRendererComponent>()
        .connect<&EnTTWorld::changed>(*this);
    registry_.on_destroy<MeshRendererComponent>().connect<&EnTTWorld::changed>(
        *this);
    for (std::size_t index = 0; index < count; ++index) {
      const auto entity = registry_.create();
      handles_[index] = entity;
      registry_.emplace<Identity>(entity, "entity/" + std::to_string(index),
                                  index);
      registry_.emplace<Transform3DComponent>(entity, initialTransform(index));
      if (index % 10 != 0) {
        registry_.emplace<MeshRendererComponent>(entity);
      }
    }
  }

  // No writable component pointers escape. patch emits on_update after
  // mutation; the registry owns both component selection and change-set
  // membership.
  void move(std::size_t index) {
    registry_.patch<Transform3DComponent>(
        handles_.at(index),
        [](auto &transform) { transform.position.x += .25F; });
  }

  void toggleMesh(std::size_t index) {
    const auto entity = handles_.at(index);
    if (registry_.remove<MeshRendererComponent>(entity) == 0) {
      registry_.emplace<MeshRendererComponent>(entity);
    }
  }

  void recreate(std::size_t index) {
    const auto old = handles_.at(index);
    // Remove subscribed components first. Their callbacks can safely mark a
    // live entity; destruction then retires its identity and dirty membership.
    registry_.remove<MeshRendererComponent>(old);
    registry_.destroy(old);
    const auto entity = registry_.create();
    handles_[index] = entity;
    registry_.emplace<Identity>(entity, "replacement/" + std::to_string(index),
                                index);
    registry_.emplace<Transform3DComponent>(entity, initialTransform(index));
    registry_.emplace<MeshRendererComponent>(entity);
  }

  std::size_t prepare(bool tracked) {
    std::size_t visited = 0;
    if (tracked) {
      for (const auto entity : registry_.view<Dirty>()) {
        refresh(entity);
        ++visited;
      }
    } else {
      // Typed views iterate relevant component pools, without a per-entity
      // type map or a membership cache maintained by the engine.
      registry_.view<Transform3DComponent, Identity>().each(
          [&](entt::entity entity, const Transform3DComponent &transform,
              const Identity &identity) {
            records_[identity.ordinal] = project(
                &transform, registry_.try_get<MeshRendererComponent>(entity));
            ++visited;
          });
    }
    registry_.clear<Dirty>();
    return visited;
  }

  const std::vector<RenderRecord> &records() const { return records_; }

private:
  void changed(entt::registry &registry, entt::entity entity) {
    registry.get_or_emplace<Dirty>(entity);
  }

  void refresh(entt::entity entity) {
    const auto ordinal = registry_.get<Identity>(entity).ordinal;
    records_[ordinal] =
        project(registry_.try_get<Transform3DComponent>(entity),
                registry_.try_get<MeshRendererComponent>(entity));
  }

  entt::registry registry_;
  std::vector<entt::entity> handles_;
  std::vector<RenderRecord> records_;
};
} // namespace demi::evaluation
