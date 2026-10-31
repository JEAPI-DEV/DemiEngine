#pragma once

#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/World.h"

namespace demi::runtime {
// Transient preparation state, never authored or serialized. Once the opening
// cover is ready, ordinary streaming retains the previous complete cover.
struct RegionalTerrainPresentation {
  bool ready = false;
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
