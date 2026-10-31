#pragma once

#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainPalette.h"
#include "demi/runtime/terrain/TerrainScatterPlacement.h"
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace demi::runtime {
struct TerrainWaterAuthoring;

struct TerrainScatterWindow {
  double minX = 0;
  double minZ = 0;
  double maxX = 0;
  double maxZ = 0;
};

struct TerrainScatterPoint {
  double x = 0;
  double z = 0;
};

struct TerrainScatterDomain {
  TerrainScatterWindow worldBounds;
  TerrainScatterPoint fieldOrigin;
  // Absolute global half-open ownership. The local field may include a halo.
  TerrainScatterWindow ownership;
};

// Knobs the palette cannot express, because they are per-terrain rather than
// per-asset.
struct TerrainScatterSettings {
  // Global multiplier on the palette's density. 0 scatters nothing, which is
  // how a recipe opts out without editing the shared palette.
  float density = 1;
  // Global multiplier on every rule's spacing. Larger is sparser.
  float spacingScale = 1;
  // Hard ceiling on returned placements, so a large field cannot exhaust
  // memory. Reaching it is reported through TerrainScatterResult::truncated
  // rather than silently accepted.
  std::size_t maximumPlacements = 1u << 20;
  // Explicit CPU work budget, checked before visiting a rule's lattice.
  std::size_t maximumCandidates = 1u << 26;
  std::size_t maximumSpacingChecks = 1u << 28;
  // Explicit global mode requires supplied constraints and a matching wet grid.
  // It does not infer regional hydrology from the local tile's boundaries.
  std::optional<TerrainScatterDomain> domain;
};

struct TerrainScatterResult {
  std::vector<TerrainScatterPlacement> placements;
  bool truncated = false;
};

// Places palette roles onto a generated field.
//
// Placement is a pure function of the field, the recipe and the palette. Every
// candidate derives its randomness from the Scatter sub-seed combined with the
// physical site it owns, never from a running sequence, so the result is
// identical regardless of iteration order, worker count, or how many times the
// field is scattered. That is the same determinism guarantee the generator
// itself makes.
//
// A physical per-rule lattice supplies stable sites independently of terrain
// resolution. Density thins sites; local priority spacing competes against the
// fixed lattice before filtering, so exclusions cannot cause distant refills.
// Rules use shared constraints, derived from the field when none are supplied.
// Explicit wet samples replace inferred sea coverage and also drive distance.
// With an explicit domain, candidate identity/spacing are world-wide while
// output positions and cell indices remain local. Ownership is half-open;
// supplied environment grids must already include matching boundary values.
// This does not derive global drainage or guarantee equality after truncation.
[[nodiscard]] TerrainScatterResult
scatterTerrain(const HeightField &field, const TerrainRecipe &recipe,
               const TerrainPalette &palette,
               const TerrainScatterSettings &settings = {},
               const class TerrainScatterConstraints *constraints = nullptr,
               std::span<const std::uint8_t> resolvedWetSamples = {},
               std::stop_token stop = {});

// The palette roles that would place on this field at all, sorted. Used to
// report a palette that cannot contribute rather than failing generation.
[[nodiscard]] std::vector<std::string>
scatterableRoles(const HeightField &field, const TerrainRecipe &recipe,
                 const TerrainPalette &palette);

// Resolves immutable candidate placements against the final authored surface.
// A filtered candidate remains in the caller's source list for later undo.
// Rechecks final biome, slope, moisture, water distance and shared exclusions;
// it thins/restores retained candidates without adding new graph candidates.
[[nodiscard]] bool refreshTerrainScatterPlacements(
    HeightField &field, std::span<const TerrainScatterPlacement> candidates,
    const TerrainRecipe &recipe, const TerrainPalette *palette,
    const TerrainWaterAuthoring *water = nullptr, std::stop_token stop = {});

} // namespace demi::runtime
