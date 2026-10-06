#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/terrain/TerrainLod.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {
struct HeightField;
struct TerrainChunk;

// What the world cares about, which is NOT the camera. A chunk that gameplay can
// reach stays resident even when the camera looks away, and a chunk nobody can
// reach may be dropped even when it is on screen. That inversion is the whole
// point of the item.
struct TerrainStreamingRelevance {
  // Gameplay anchors in world space: the player, objectives, anything a
  // gameplay query can reach. Chunks near one are always resident.
  std::vector<Vec3> gameplayAnchors;
  // Beyond this from every anchor AND off screen, a chunk is a candidate for
  // eviction. Zero means camera-only.
  float gameplayRadius = 0.F;
  // A chunk never leaves its current level, and never leaves memory, before it
  // has been unsatisfied for this long. Zero removes the delay entirely.
  //
  // The delay is ASYMMETRIC on purpose, and the asymmetry is the whole design:
  // a FINER level is taken immediately, a COARSER one and an eviction wait the
  // full delay. Reacting late to something entering view is far less noticeable
  // than a chunk flickering as the camera sits on a threshold, so the policy
  // spends its latency budget on the transition the player cannot see and none
  // on the one they would.
  float hysteresisSeconds = 0.75F;
  // A chunk never drops below this level while resident. Read as the coarsest
  // level that still has a mesh: `Off` means "not resident", so it is clamped to
  // `Coarse` rather than allowed to make a resident chunk have no geometry.
  TerrainLod minimumResidentLod = TerrainLod::Quarter;
  TerrainLodSettings lod{};
};

struct TerrainChunkResidency {
  // Index into the field's own chunk list, so a caller resolves a residency back
  // to the rectangle it was decided about without searching by position.
  std::size_t chunkIndex = 0;
  int firstCellX = 0, firstCellZ = 0, cellsX = 0, cellsZ = 0;
  TerrainLod level = TerrainLod::Full;
  bool resident = true;
  // True while the chunk is held by a gameplay anchor rather than by the camera.
  bool gameplayHeld = false;
  // Continuous seconds the chunk has been outside the view distance AND outside
  // every anchor's radius. Zero the moment anything wants it again.
  float outsideForSeconds = 0.F;
  // How long the level selector has asked for something COARSER than the level
  // this chunk currently holds. Zero while the chunk is at the wanted level, and
  // this is what a demotion waits out.
  float coarserForSeconds = 0.F;
  // Why the chunk is at this level, so an editor overlay can explain it. Never
  // empty, and always names the level it settled on.
  std::string reason;
};

struct TerrainStreamingState {
  // One entry per field chunk, in the field's own chunk order.
  std::vector<TerrainChunkResidency> chunks;
  // How many chunks are resident and how many are not. Together they always
  // partition `chunks`.
  std::size_t resident = 0;
  std::size_t evicted = 0;
  // THIS UPDATE's level changes, between two resident states only: a chunk that
  // has just appeared, or has just been evicted and is coming back, adopts its
  // wanted level immediately and counts as neither. Mixing those in would hide
  // the level churn these two counters exist to measure.
  std::size_t promoted = 0; // level got FINER this update
  std::size_t demoted = 0;  // level got COARSER this update
  // An eviction is deliberately NOT a demotion: the chunk stops existing rather
  // than getting blurrier, so a shrinking resident set and a blurring one stay
  // separately visible.
  //
  // Every resident chunk's triangle count at its own level, from the chunk's real
  // size and `terrainLodStep`. Two triangles per quad of the reduced lattice, so
  // this is exactly what `buildTerrainLodMesh` would emit for these levels minus
  // any skirt. A caller can budget resident terrain from a state alone, having
  // loaded nothing.
  [[nodiscard]] std::size_t triangles() const;
};

// Advances residency by dt for one terrain.
//
// THE RESIDENCY RULE, in the order it is applied to each chunk:
//   1. Nothing is wanted when the chunk is outside `viewDistance` AND outside
//      every anchor's `gameplayRadius`. That is the only eviction candidate, and
//      neither radius is a quality level: `viewDistance` bounds residency, and
//      the LEVEL always comes from the LOD selector over the camera's distance.
//   2. A chunk inside any anchor's radius is `gameplayHeld`. It cannot be
//      evicted and cannot fall below `minimumResidentLod`, whatever the camera
//      does. The anchor decides residency and the floor, never the level.
//   3. Otherwise the chunk is resident at `terrainLodForChunk` for its distance
//      to the chunk's NEAREST point, clamped so it is never coarser than
//      `minimumResidentLod`.
//   4. A FINER level is taken on this update. A COARSER one waits until the
//      chunk has asked for it, continuously, for `hysteresisSeconds`; so does an
//      eviction, whose clock is `outsideForSeconds`.
//
// Pure in (previous, field, camera, dt, relevance): same inputs, same state, no
// world, no entity and no renderer touched. It returns a NEW state rather than
// mutating the old one, so a caller can diff the two and keep the previous one
// while a chunk build is still in flight.
//
// Throws std::invalid_argument, with a message naming the problem, on a negative
// or non-finite `dt`, a negative or non-finite `viewDistance`, a negative grid
// extent, a non-finite camera or anchor, or a chunk list that does not tile the
// grid. A negative `gameplayRadius` or `hysteresisSeconds` is normalised to zero
// rather than refused, because zero is already the documented meaning of both.
//
// `previous` may be empty, or describe a different field: entries are matched by
// position AND verified against the field's chunk rectangle, and one that does
// not match is ignored rather than misapplied. The result always has exactly
// one entry per field chunk, in field order, so a chunk can never be lost or
// duplicated by an update.
[[nodiscard]] TerrainStreamingState
updateTerrainStreaming(const TerrainStreamingState &previous,
                       const HeightField &field, Vec2 cameraPosition,
                       float viewDistance, float dt,
                       const TerrainStreamingRelevance &);

// The level a chunk should hold, or nothing when it should be evicted. Split out
// so a caller can ask about one chunk without advancing the whole state, for a
// tool that wants to show what streaming would do without adopting it.
//
// This is the WANTED level, with the eviction half of the delay already applied:
// `outsideForSeconds` is how long the caller has already seen the chunk unsatisfied,
// and while it is shorter than `hysteresisSeconds` the chunk is still wanted and
// this returns the level it would hold. A caller that is mid-transition and needs
// the DEMOTION delay as well must keep the state and use updateTerrainStreaming:
// how long a chunk has wanted something coarser is a property of the transition,
// not of the geometry, so it cannot be answered without one.
//
// Never evicts a gameplay-held chunk, and never names a level coarser than
// `minimumResidentLod` for a chunk it keeps.
//
// Throws std::invalid_argument when the chunk does not fit inside the field, when
// `viewDistance` is negative or non-finite, or when the camera is not finite.
// Tiling is a property of the whole chunk list and is checked by
// updateTerrainStreaming, which is the only entry point that sees the list.
[[nodiscard]] std::optional<TerrainLod>
terrainChunkTargetLevel(const HeightField &field, const TerrainChunk &,
                        Vec2 cameraPosition, float viewDistance,
                        const TerrainStreamingRelevance &, float outsideForSeconds);
} // namespace demi::runtime
