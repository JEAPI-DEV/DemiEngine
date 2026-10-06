#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/terrain/TerrainSamples.h"
#include "demi/runtime/terrain/TerrainScatterPlacement.h"

#include <memory>
#include <string>
#include <vector>

namespace demi::runtime {
struct TerrainGraphArtifacts;

struct TerrainChunk {
  int firstCellX = 0;
  int firstCellZ = 0;
  int cellsX = 0;
  int cellsZ = 0;
};

// Generated data shared by sampling, cooking, physics and rendering. These
// consumers do not need the generator or its authored recipe to read a field.
struct HeightField {
  Vec2 size{};
  int cellsX = 0;
  int cellsZ = 0;
  TerrainSamples<float> baseHeights;
  TerrainSamples<float> heights;
  TerrainSamples<Vec3> normals;
  TerrainSamples<std::size_t> biomeIndices;
  TerrainSamples<float> exclusions;
  std::vector<std::string> biomeIds;
  std::vector<Color> biomeColors;
  std::vector<std::string> biomeMaterials;
  std::vector<float> biomeTextureScales;
  const std::string &biomeMaterial(std::size_t index) const {
    static const std::string defaultMaterial;
    return biomeMaterials.empty() ? defaultMaterial : biomeMaterials.at(index);
  }
  float biomeTextureScale(std::size_t index) const {
    return biomeTextureScales.empty() ? 1.F : biomeTextureScales.at(index);
  }
  std::vector<TerrainChunk> chunks;
  std::vector<TerrainScatterPlacement> scatterPlacements;
  bool scatterTruncated = false;
  std::string paletteId;
  // Asset contents participate in prepared-data identity, not just asset IDs.
  std::string inputFingerprint;
  std::string stageOrder;
  std::string quality = "standard";
  std::shared_ptr<const TerrainGraphArtifacts> graphArtifacts;
  std::shared_ptr<const TerrainPalette> resolvedPalette;

  std::size_t index(int x, int z) const;
  Vec2 position(int x, int z) const;
  float height(int x, int z) const;
  Vec3 normal(int x, int z) const;
};
} // namespace demi::runtime
