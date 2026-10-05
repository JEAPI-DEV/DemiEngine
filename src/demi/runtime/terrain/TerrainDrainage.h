#pragma once

#include "demi/runtime/terrain/TerrainGeneration.h"
#include <cstddef>
#include <optional>
#include <stop_token>
#include <vector>

namespace demi::runtime {

// Stored in TerrainDrainage::downstream for a sample that has nowhere lower to
// drain to: a field outlet on the border, or an interior sink. Named so a
// consumer never has to know that the sentinel is an all-ones index, and so a
// forgotten bounds check fails loudly instead of aliasing the last sample.
inline constexpr std::size_t terrainNoDownstream = ~std::size_t{0};

// Where water on a finished surface would go, and what it would collect on the
// way. Computed once by the Drainage stage because every later consumer needs
// the same answer: hydrology carves channels along it, biome rules band on flow,
// erosion walks it, and scattering keeps off the water it drains to.
//
// Flow is accumulation normalised to 0..1 against the field's own maximum, so
// it is comparable between fields without either field's absolute area leaking
// into the mask.
struct TerrainDrainage {
  // Normalised accumulation, 0..1. Zero at the border: a border sample is the
  // field's own outlet, and it has no upstream area inside the field to report.
  TerrainSamples<float> flow;
  // World units to the nearest sample at or below sea level. With no water
  // anywhere in the field every sample is left maximally distant rather than
  // zero, so a water-distance band cannot match a dry field by accident.
  TerrainSamples<float> waterDistance;
  // Basin id per sample. 0 is the field itself: the sample drains off the edge.
  // Every closed basin gets its own id, numbered from 1 in row-major order of
  // first encounter, so identical input always yields identical ids.
  TerrainSamples<std::size_t> basin;
  // Number of closed basins. Basin ids therefore run 1..basinCount, and a field
  // that drains entirely off its edge has none.
  std::size_t basinCount = 0;
  // Where each basin's water collects: basinOutlets[0] is the field's own outlet
  // and basinOutlets[b] is the lowest sample of basin b. So basinOutlets[b] is a
  // valid read for any id found in `basin`, and it holds basinCount + 1 entries.
  std::vector<Vec3> basinOutlets;
  // The sample below which water stands, in world units.
  float seaLevel = 0;
  // The sample each one drains into, or terrainNoDownstream at an outlet or
  // sink. This is the direction the erosion stage walks instead of computing a
  // second priority flood: the tree is already here, and re-deriving it would
  // let the two disagree.
  TerrainSamples<std::size_t> downstream;
};

struct TerrainDrainageSettings {
  float seaLevel = 0;
  // Recorded for the caller to attribute the result to a tier. It deliberately
  // does not change the numbers: flow, basins and water distance are all
  // consumed by rules, hydrology and masks, which run in Preview too, and a
  // coarser drainage would label basins against the full-resolution surface they
  // get joined to. Preview buys its iteration speed in the Erosion stage.
  TerrainQuality quality = TerrainQuality::Standard;
};

// Empty only when the field holds no samples. A field whose sample storage does
// not match its own grid throws std::invalid_argument rather than computing
// drainage over a grid that is not the surface.
std::optional<TerrainDrainage>
computeTerrainDrainage(const HeightField &field, const TerrainRecipe &recipe,
                       const TerrainDrainageSettings &settings,
                       std::stop_token stop = {});

} // namespace demi::runtime
