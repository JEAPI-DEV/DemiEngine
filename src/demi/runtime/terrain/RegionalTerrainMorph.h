#pragma once

#include "demi/runtime/terrain/RegionalTerrainCover.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace demi::runtime {
struct World;

// A complete rendered cover, including its stitched edge samples. Interpolate
// its actual triangles, not the procedural function or a bilinear surface.
class RegionalTerrainCoverSurface {
public:
  RegionalTerrainCoverSurface(
      const TerrainRegionConfig &config,
      std::map<TerrainRegionPatchKey, std::shared_ptr<const HeightField>>
          fields);
  [[nodiscard]] float height(double x, double z) const;
  [[nodiscard]] const HeightField *field(TerrainRegionPatchKey key) const;

private:
  TerrainRegionRectangle bounds_;
  std::map<TerrainRegionPatchKey, std::shared_ptr<const HeightField>> fields_;
  std::uint32_t maximumLevel_ = 0;
};

[[nodiscard]] std::shared_ptr<const HeightField>
makeRegionalMorphField(const TerrainRegionConfig &config,
                       TerrainRegionPatchKey key, const HeightField &grid,
                       const RegionalTerrainCoverSurface &from,
                       const RegionalTerrainCoverSurface &to);

// Render-only state. Physics retains the ordinary immutable collision snapshot.
struct TerrainVisualMorph {
  float amount = 1;
};
struct RegionalTerrainMorphPatch {
  std::string owner;
  std::shared_ptr<const HeightField> field;
  std::vector<std::string> surfaces;
};

// One common clock drives every changed patch and stitched neighbor. A split
// morphs new fine geometry; a merge morphs old fine geometry before retiring
// it.
class RegionalTerrainMorph {
public:
  RegionalTerrainMorph(World &world,
                       std::vector<RegionalTerrainMorphPatch> patches);
  // Returns true after the end shape has reached every GPU/CPU render path.
  bool advance(World &world, float deltaSeconds);

private:
  void apply(World &world, float amount);
  std::vector<RegionalTerrainMorphPatch> patches_;
  float elapsed_ = 0;
};
} // namespace demi::runtime
