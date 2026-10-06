#include "demi/runtime/terrain/TerrainGeneration.h"

namespace demi::runtime {

std::string_view terrainStageName(const TerrainStage stage) {
  switch (stage) {
  case TerrainStage::Landform:
    return "landform";
  case TerrainStage::Drainage:
    return "drainage";
  case TerrainStage::Erosion:
    return "erosion";
  case TerrainStage::Hydrology:
    return "hydrology";
  case TerrainStage::Masks:
    return "masks";
  case TerrainStage::Surface:
    return "surface";
  case TerrainStage::Sculpt:
    return "sculpt";
  }
  return "unknown";
}

std::string_view terrainQualityName(const TerrainQuality quality) {
  return quality == TerrainQuality::Preview ? "preview" : "standard";
}

bool terrainStageRan(const TerrainGenerationResult &result,
                     const TerrainStage stage) {
  for (const auto ran : result.stages)
    if (ran == stage)
      return true;
  return false;
}

} // namespace demi::runtime
