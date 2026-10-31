#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/terrain/TerrainSamples.h"
#include "demi/runtime/terrain/TerrainScatterPlacement.h"

#include <memory>
#include <string>
#include <vector>

namespace demi::runtime {
struct TerrainGraphArtifacts;
struct TerrainSurfaceMaterials;

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
  // Runtime regional tiles share a source-local material frame. Finite cooked
  // fields keep zero; tile payloads must not masquerade as finite asset caches.
  Vec2 projectionOrigin{};
  int cellsX = 0;
  int cellsZ = 0;
  TerrainSamples<float> baseHeights;
  TerrainSamples<float> heights;
  // Transient render transition endpoints, excluded from authored/cooked data.
  std::shared_ptr<const TerrainSamples<float>> morphFromHeights;
  // Signed erosion transport of the selected graph surface, after weighted
  // blends. Independent per-node ledgers remain diagnostic provenance.
  std::shared_ptr<const TerrainSamples<float>> surfaceSedimentBalance;
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
  std::shared_ptr<const TerrainSurfaceMaterials> surfaceMaterials;
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
