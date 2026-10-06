#pragma once

#include "demi/runtime/terrain/TerrainSamples.h"

namespace demi::runtime {
// Derived sample data is independent of the generation pipeline that produced
// it. Sampling and hydrology consumers need no recipe or graph scheduler.
struct TerrainMasks {
  TerrainSamples<float> slope;    // Degrees from horizontal.
  TerrainSamples<float> moisture; // Normalized saturation.
  TerrainSamples<float> waterDistance;
  TerrainSamples<float> flow; // Normalized upstream accumulation, not velocity.
  TerrainSamples<float> sediment;
  TerrainSamples<std::size_t> substrate;

  std::size_t count() const { return slope.size(); }
  bool empty() const { return slope.size() == 0; }
};
} // namespace demi::runtime
