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
// renderer or a world; turning a placement into an entity is the consumer's
// job.
struct TerrainScatterPlacement {
  std::string ruleId;
  std::string model;
  std::string prefab;
  // Index into the generated field's biomeIds, kept as the name for
  // diagnostics.
  std::size_t biome = 0;
  std::string biomeName;
  // Environment-sampling reference only. Resolution changes can change this
  // index without changing the physical candidate or its durable identity.
  std::size_t cell = 0;
  int candidateX = 0;
  int candidateZ = 0;
  Vec3 position;
  float yaw = 0;
  float scale = 1;
  TerrainCollisionPolicy collision = TerrainCollisionPolicy::Static;
};

} // namespace demi::runtime
