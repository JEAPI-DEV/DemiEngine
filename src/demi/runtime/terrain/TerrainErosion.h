#pragma once

#include "demi/runtime/terrain/TerrainDrainage.h"
#include "demi/runtime/terrain/TerrainErosionTransport.h"
#include <optional>
#include <stop_token>

namespace demi::runtime {

// Hydraulic and thermal transport over an already drained surface.
//
// Droplets follow currently descending terrain mesh edges. Input drainage
// guides equal-slope and flat choices; it never forces a now-uphill step.
struct TerrainErosionSettings {
  // Hydraulic iterations only. Zero skips droplets; Standard thermal
  // relaxation remains controlled independently by thermalStrength.
  int iterations = 40;
  float rainRate = 0.02F;
  float sedimentCapacity = 4.0F;
  float depositionRate = 0.3F;
  float evaporation = 0.02F;
  float thermalStrength = 0.35F;
  // The angle of repose, as a rise over run. A neighbour steeper than this
  // sheds material until it is not. Run uses physical X/Z sample spacing,
  // including Euclidean diagonal distance on anisotropic grids.
  float talusSlope = 0.6F;
  TerrainQuality quality = TerrainQuality::Standard;
  // The world seed erosion derives its droplet stream from, through the Erosion
  // seed channel. The generator sets it from recipe.seed; it lives here because
  // the stage runs without the recipe.
  int seed = 1337;
};

// Returns a new height sample set and leaves the field untouched.
//
// Empty only when the field holds no samples. A field whose samples do not
// match the drained surface, or a drainage that does not match the field,
// throws std::invalid_argument rather than eroding along a direction that
// belongs to some other surface.
//
// sedimentOut, when given, receives the signed transport balance per sample:
// positive where material was deposited, negative where it was cut. That is the
// same quantity TerrainMasks::sediment exposes, so a consumer does not have to
// diff two surfaces to recover it. Optional outputs are published together
// only on success; cancellation leaves them unchanged.
std::optional<TerrainSamples<float>>
applyTerrainErosion(const HeightField &field, const TerrainDrainage &drainage,
                    const TerrainErosionSettings &settings,
                    TerrainSamples<float> *sedimentOut = nullptr,
                    std::stop_token stop = {},
                    const TerrainGenerator::Progress &progress = {},
                    TerrainErosionTransport *transportOut = nullptr);

} // namespace demi::runtime
