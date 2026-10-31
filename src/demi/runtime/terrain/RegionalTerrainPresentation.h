#pragma once

#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace demi::runtime {
// Presentation-thread handshake. The terrain service owns immutable requests;
// a live renderer acknowledges them after asynchronous GPU preparation. A weak
// lease lets headless/hidden worlds keep streaming without a renderer barrier.
struct RegionalTerrainRenderPatch {
  std::shared_ptr<const HeightField> field;
  Vec3 boundsMin;
  Vec3 boundsMax;
};

struct RegionalTerrainRenderPreparation {
  std::vector<RegionalTerrainRenderPatch> active;
  std::vector<RegionalTerrainRenderPatch> pending;
  std::weak_ptr<void> renderer;
  bool prepared = false;
  std::uint64_t presentation = 0;

  [[nodiscard]] bool waiting() const {
    return !pending.empty() && !renderer.expired() && !prepared;
  }
};

// Transient preparation state, never authored or serialized. Once the opening
// cover is ready, ordinary streaming retains the previous complete cover.
struct RegionalTerrainPresentation {
  bool ready = false;
  std::shared_ptr<RegionalTerrainRenderPreparation> render;
};

inline bool regionalTerrainPreparing(const World &world) {
  for (const auto &entity : world.entities) {
    const auto *terrain = entity.component<Terrain3DComponent>();
    if (!entity.enabled || !terrain || !terrain->region) {
      continue;
    }
    const auto *presentation = entity.component<RegionalTerrainPresentation>();
    if (!presentation || !presentation->ready) {
      return true;
    }
  }
  return false;
}
} // namespace demi::runtime
