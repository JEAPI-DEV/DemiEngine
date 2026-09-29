#pragma once

#include "demi/runtime/terrain/TerrainRecipe.h"
#include "demi/runtime/terrain/TerrainSamples.h"
#include <functional>
#include <optional>
#include <stop_token>

namespace demi::runtime {

struct TerrainChunk {
  int firstCellX = 0;
  int firstCellZ = 0;
  int cellsX = 0;
  int cellsZ = 0;
};

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
  std::vector<TerrainChunk> chunks;

  // Row-major global samples; adjacent chunks reference the same edge samples.
  std::size_t index(int x, int z) const;
  Vec2 position(int x, int z) const;
  float height(int x, int z) const;
  Vec3 normal(int x, int z) const;
};

class TerrainGenerator {
public:
  using Progress = std::function<void(float)>;
  // Empty only on cancellation. Invalid recipes throw invalid_argument;
  // capacity/allocation errors propagate. Progress is monotonic in [0,1].
  static std::optional<HeightField> generate(const TerrainRecipe &recipe,
                                             std::stop_token stop = {},
                                             const Progress &progress = {});
};

TerrainEdit createProtectionEdit(const HeightField &field, Vec2 center,
                                 float radius, float strength = 1,
                                 float falloff = 0);

} // namespace demi::runtime
