#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/terrain/TerrainPalette.h"
#include "demi/runtime/terrain/TerrainScatterPlacement.h"
#include "demi/runtime/terrain/TerrainSeed.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {

// A stable identity that survives regeneration, and the metadata a renderer
// needs. Identity is derived from the placement's own inputs, never from its
// index, so inserting a placement does not renumber the ones after it.
//
// This is a description, like TerrainScatterPlacement. Turning one into a GPU
// instance is the renderer's job; what lives here is everything the renderer
// would otherwise have to re-derive, and deriving it twice is how two systems
// end up disagreeing about which mesh a placement wants.
struct TerrainScatterInstance {
  // Stable across regenerations. Derived by terrainScatterInstanceKey, never by
  // the placement's position in a vector.
  std::string id;
  // The sub-seed the yaw and scale jitter came from, so a renderer that wants a
  // per-instance phase animation gets the same stream the placement used
  // instead of a second one that would not match after a regeneration.
  std::uint64_t seed = 0;
  std::size_t cell = 0;
  std::string ruleId;
  std::size_t biome = 0;
  std::string model;
  std::string prefab;
  Vec3 position;
  float yaw = 0;
  float scale = 1;
  TerrainCollisionPolicy collision = TerrainCollisionPolicy::Static;
  int lod = 0;
  // Render grouping, so an instancer can batch without re-deriving anything.
  // Assigned by terrainScatterGroupInstances; it is a property of the whole
  // set, not of this instance, which is why it is excluded from the hash.
  std::size_t instanceGroup = 0;
};

// The identity of one scattered instance, as a readable string.
//
// The identity MUST change when any of its inputs change, and MUST NOT change
// when an unrelated placement is added or removed. That is the whole contract:
// it is what lets a region be re-scattered while every other region keeps the
// instances a renderer already uploaded, and what lets a placement be promoted
// to an authored object that a later regeneration must not replace.
//
// `regionId` and `layerId` exist because a region or layer may be re-scattered
// with a different palette: without them a region-scoped decoration would claim
// the same identity as the terrain-level one it sits beside. They default to
// empty, which is the single terrain-level layer.
//
// Segments are escaped rather than joined raw, because an id containing the
// separator would otherwise let two different input sets produce one identical
// key.
[[nodiscard]] std::string
terrainScatterInstanceKey(std::string_view paletteId, std::string_view ruleId,
                          std::size_t cell, std::string_view regionId = {},
                          std::string_view layerId = {});

// The seed stream for one placement's jitter.
//
// The derivation is deliberately identical to the one scatterTerrain() uses for
// the sample slot that produces yaw and scale, so an instance's recorded seed is
// the seed its own numbers came from rather than a plausible-looking unrelated
// value.
[[nodiscard]] std::uint64_t terrainScatterInstanceSeed(int worldSeed,
                                                       std::string_view ruleId,
                                                       std::size_t cell);

// Fills an instance from a placement, deriving the id and the seed.
[[nodiscard]] TerrainScatterInstance
terrainScatterInstanceFrom(const TerrainScatterPlacement &placement,
                           std::string_view paletteId, int worldSeed,
                           std::string_view regionId = {},
                           std::string_view layerId = {});

// A digest of everything that defines this instance.
//
// Equal hashes mean equal instances: the same identity resolving to the same
// asset, transform, collision and LOD, so a consumer can skip an upload it has
// already sent. The grouping index is deliberately excluded, because grouping is
// derived from the rest of the set and folding it in would make an unrelated
// placement change an existing instance's hash, which is the exact failure the
// identity contract exists to prevent.
//
// Floats are compared by their exact bit pattern, with -0.0 folded onto 0.0 and
// each non-finite class folded onto one canonical pattern, so a hash never
// depends on a NaN payload or on which of two equal reals the arithmetic
// happened to produce.
[[nodiscard]] std::uint64_t
terrainScatterInstanceHash(const TerrainScatterInstance &instance);

// One instancer batch.
struct TerrainScatterInstanceGroup {
  // The drawable this batch draws, which is the asset when the palette named
  // one and the prefab when it did not. Two instances are batchable when this
  // matches, which is the question an instancer actually asks.
  std::string model;
  std::size_t instances = 0;
  // Indices into the vector passed in, ascending by instance id so a batch is
  // deterministic whatever order the placements arrived in.
  std::vector<std::size_t> members;
};

// Assigns every instance's instanceGroup and returns the batches.
//
// Groups are keyed on the drawable and numbered from the sorted distinct
// drawables, so group numbers are dense, deterministic and independent of the
// order the instances are visited in. Only the set of drawables changes the
// numbering, never an individual instance's identity.
[[nodiscard]] std::vector<TerrainScatterInstanceGroup>
terrainScatterGroupInstances(std::vector<TerrainScatterInstance> &instances);

// Collision and LOD are decided per instance, never per placement wholesale.
//
// None is the whole reason this is per instance: a forest of grass and a
// handful of boulders are one palette and one placement pass, and giving the
// grass a physics body would cost the same as giving the boulders one and buy
// nothing.
[[nodiscard]] bool
terrainScatterWantsCollision(const TerrainScatterInstance &instance);

// Why collision was skipped, for a density report. Empty when collision is
// wanted. Never empty-and-silent: a decorative rule that produces no collider
// is a decision, and a report that cannot name it is indistinguishable from a
// bug.
[[nodiscard]] std::string_view
terrainScatterCollisionSkipReason(const TerrainScatterInstance &instance);

// The LOD level this instance draws at, always within [0, instance.lod].
//
// instance.lod is the palette's coarsest level, so the range has levels+1
// entries and the function can never return an index a mesh array does not have.
// `lodDistance` is where the finest level holds; `cullDistance` is where the
// coarsest one has certainly taken over; the levels between are distributed
// evenly across that span.
//
// Two deliberate degenerate cases, both of which mean "no distance switching":
//   lodDistance <= 0 -> level 0 always, because a zero LOD distance must not be
//     read as "already past every threshold" and collapse to the coarsest mesh.
//   cullDistance <= 0 (or not past lodDistance) -> the full range is decided by
//     lodDistance alone: level 0 up to it, the coarsest level beyond it.
[[nodiscard]] int terrainScatterLodFor(const TerrainScatterInstance &instance,
                                       float distance, float lodDistance,
                                       float cullDistance);

} // namespace demi::runtime
