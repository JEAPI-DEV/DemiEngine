#pragma once

#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainPalette.h"
#include "demi/runtime/terrain/TerrainScatterPlacement.h"
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace demi::runtime {
struct TerrainWaterAuthoring;

// Knobs the palette cannot express, because they are per-terrain rather than
// per-asset.
struct TerrainScatterSettings {
  // Global multiplier on the palette's weights. 0 scatters nothing, which is how
  // a recipe opts out without editing the shared palette.
  float density = 1;
  // Global multiplier on every rule's spacing. Larger is sparser.
  float spacingScale = 1;
  // Hard ceiling on returned placements, so a large field cannot exhaust memory.
  // Reaching it is reported through TerrainScatterResult::truncated rather than
  // silently accepted.
  std::size_t maximumPlacements = 1u << 20;
};

struct TerrainScatterResult {
  std::vector<TerrainScatterPlacement> placements;
  bool truncated = false;
};

// Places palette roles onto a generated field.
//
// Placement is a pure function of the field, the recipe and the palette. Every
// candidate derives its randomness from the Scatter sub-seed combined with the
// cell it sits in, never from a running sequence, so the result is identical
// regardless of iteration order, worker count, or how many times the field is
// scattered. That is the same determinism guarantee the generator itself makes.
//
// Roles only place on cells whose biome they allow, on unexcluded cells, and
// never on water. Spacing is enforced with a uniform spatial hash, so the cost
// is O(cells x roles) and does not grow with the number of accepted instances.
//
// When constraints are supplied they are the single answer to "may something be
// placed here", so the editor's explanation and the solver's decision cannot
// disagree. Null keeps the inline water and exclusion tests this function
// always had, which is what a caller with no constraint authoring wants.
[[nodiscard]] TerrainScatterResult
scatterTerrain(const HeightField &field, const TerrainRecipe &recipe,
               const TerrainPalette &palette,
               const TerrainScatterSettings &settings = {},
               const class TerrainScatterConstraints *constraints = nullptr);

// The palette roles that would place on this field at all, sorted. Used to
// report a palette that cannot contribute rather than failing generation.
[[nodiscard]] std::vector<std::string>
scatterableRoles(const HeightField &field, const TerrainRecipe &recipe,
                 const TerrainPalette &palette);

// Resolves immutable candidate placements against the final authored surface.
// A filtered candidate remains in the caller's source list for later undo.
[[nodiscard]] bool refreshTerrainScatterPlacements(
    HeightField &field, std::span<const TerrainScatterPlacement> candidates,
    const TerrainWaterAuthoring *water = nullptr, std::stop_token stop = {});

} // namespace demi::runtime
