#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/terrain/TerrainPalette.h"
#include <string>
#include <vector>

namespace demi::runtime {

// One resolved instance to spawn on the terrain surface.
//
// This is deliberately a description, not an engine entity. Deciding where
// something belongs is a generation concern that must stay testable without a
// renderer or a world; turning a placement into an entity is the consumer's job.
struct TerrainScatterPlacement {
  TerrainPaletteRole role = TerrainPaletteRole::Soil;
  std::string asset;
  std::string prefab;
  // Index into the generated field's biomeIds, kept as the name for diagnostics.
  std::size_t biome = 0;
  std::string biomeName;
  // The cell this placement belongs to, in the field's sample index space. This
  // is the stable identity a consumer reconciles on: a sculpt stroke moves the
  // ground under an instance without changing which cell owns it, so the
  // instance is updated rather than destroyed and rebuilt.
  std::size_t cell = 0;
  Vec3 position;
  float yaw = 0;
  float scale = 1;
  TerrainCollisionPolicy collision = TerrainCollisionPolicy::Static;
  int lod = 0;
};

} // namespace demi::runtime
