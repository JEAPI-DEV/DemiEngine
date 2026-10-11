#pragma once

#include "demi/runtime/terrain/TerrainSamples.h"
#include <cstdint>
#include <map>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace demi::runtime {
struct HeightField;
struct TerrainRecipe;
struct TerrainPalette;

// Immutable classification is independent of the material assets bound to it.
struct TerrainSurfaceClassification {
  // An empty role records a sample for which no surface rule scored above zero.
  std::vector<std::string> roles;
  TerrainSamples<std::uint8_t> samples;
};
struct TerrainSurfaceMaterials {
  std::string materialSet;
  std::map<std::string, std::string, std::less<>> bindings;
  std::shared_ptr<const TerrainSurfaceClassification> classification;
  std::vector<std::string> unboundRoles;
};

bool terrainSurfaceMaterialsEqual(const HeightField &left,
                                  const HeightField &right);

bool terrainSurfaceBindingsMatch(const HeightField &, const TerrainPalette *);
// Returns false on cancellation. Does not modify heights, normals or biomes.
// Reclassification is explicit because water-distance masks can be nonlocal.
bool updateTerrainSurfaceMaterials(HeightField &, const TerrainRecipe &,
                                    const TerrainPalette *, bool reclassify,
                                    std::stop_token stop = {});
} // namespace demi::runtime
