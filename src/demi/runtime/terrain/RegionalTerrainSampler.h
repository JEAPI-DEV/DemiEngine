#pragma once

#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainRecipe.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace demi::runtime {
struct TerrainRegionPatchKey;
struct TerrainPalette;

// Uses the normal TerrainRecipe and graph contract. The wrapper contributes
// only world placement and canonical sampling, never a second terrain DSL.
struct TerrainRegionStreamingSettings {
  double lodDistanceRatio = 2;
  std::size_t maximumResidentBytes = 128U * 1024U * 1024U;
  std::size_t maximumPendingJobs = 2;
  std::uint32_t vegetationCellSamples = 128;
  double vegetationRadius = 512;
  // A separate, sparse canopy layer prevents long tree views from requesting
  // dense ground cover over the same area. Zero keeps a single shared layer.
  double vegetationCanopyRadius = 0;
  std::uint32_t vegetationCanopyCellSamples = 512;
};
struct TerrainRegionConfig {
  TerrainRecipe recipe;
  std::shared_ptr<const TerrainPalette> palette;
  std::filesystem::path projectDirectory;
  TerrainRegionStreamingSettings streaming;
  double originX = 0;
  double originZ = 0;
  double baseSampleSpacing = 0;
  double normalSampleSpacing = 0;
};

struct TerrainRegionTileRequest {
  // Integer coordinates on the region's common base lattice. Coarser tiles
  // select a stride on this lattice, rather than rescaling a local noise field.
  std::int64_t firstSampleX = 0;
  std::int64_t firstSampleZ = 0;
  std::uint32_t cellsX = 0;
  std::uint32_t cellsZ = 0;
  std::uint32_t stride = 0;
};

struct TerrainRegionTile {
  double originX = 0;
  double originZ = 0;
  double sampleSpacing = 0;
  HeightField field;
};

class RegionalTerrainSampler {
public:
  explicit RegionalTerrainSampler(TerrainRegionConfig config);
  [[nodiscard]] const TerrainRegionConfig &config() const;
  // Sampling includes a one-normal-step halo outside the authored world bounds.
  // There is no independent per-tile erosion, random sequence or boundary
  // clamp.
  [[nodiscard]] float sampleHeight(double globalX, double globalZ) const;
  [[nodiscard]] Vec3 sampleNormal(double globalX, double globalZ) const;
  [[nodiscard]] TerrainRegionTile sampleTile(TerrainRegionTileRequest request,
                                             std::stop_token stop = {}) const;
  [[nodiscard]] bool hasScatter() const;
  // Exact extrema on the patch's unstitched lattice, without material/normal
  // preparation. Stitched edge interpolation stays inside these bounds.
  [[nodiscard]] std::pair<float, float>
  patchHeightBounds(TerrainRegionPatchKey patch,
                    std::stop_token stop = {}) const;
  // Dyadic patch grid covers arbitrary rectangular extents exactly. Base
  // spacing is the finest requested spacing bound; normal footprint is fixed.
  [[nodiscard]] TerrainRegionTile samplePatch(TerrainRegionPatchKey patch,
                                              std::uint8_t stitchMask = 0,
                                              std::stop_token stop = {}) const;

private:
  struct Impl;
  std::shared_ptr<const Impl> impl_;
};

} // namespace demi::runtime
