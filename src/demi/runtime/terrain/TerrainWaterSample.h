#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include <string>

namespace demi::runtime {
struct TerrainWaterSample {
  std::string terrainId;
  std::string bodyId;
  std::string sceneId;
  std::string kind;
  Vec3 surface;
  Vec3 normal;
  Vec3 columnUp{0, 1, 0};
  Vec3 flowVelocity;
  float depth = 0;
  bool underwater = false;
};
} // namespace demi::runtime
