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
  float depth = 0;
  bool underwater = false;
};
} // namespace demi::runtime
