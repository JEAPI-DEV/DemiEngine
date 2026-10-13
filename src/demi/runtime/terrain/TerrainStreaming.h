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

// Distances and positions are terrain-local. This policy has no scene transform
// input and does not resolve visibility, physics residency, or runtime loading.
struct TerrainStreamingRelevance {
  // Local x/z positions that keep nearby chunks resident. Height is ignored.
  std::vector<Vec3> gameplayAnchors;
  // Zero disables gameplay retention. Negative finite values normalize to zero.
  float gameplayRadius = 0.0F;
  // Promotions are immediate; coarsening and eviction wait this duration.
  // Negative finite values normalize to zero.
  float hysteresisSeconds = 0.75F;
  // Coarsest resident level. Off normalizes to Coarse.
  TerrainLod minimumResidentLod = TerrainLod::Quarter;
  TerrainLodSettings lod{};
};

struct TerrainChunkResidency {
  std::size_t chunkIndex = 0;
  int firstCellX = 0;
  int firstCellZ = 0;
  int cellsX = 0;
  int cellsZ = 0;
  TerrainLod level = TerrainLod::Full;
  bool resident = true;
  bool gameplayHeld = false;
  // Continuous time outside both view distance and gameplay radius.
  float outsideForSeconds = 0.0F;
  // Continuous time requesting any coarser level than the held level.
  float coarserForSeconds = 0.0F;
  std::string reason;
};

struct TerrainStreamingState {
  // Entries preserve the field's chunk order.
  std::vector<TerrainChunkResidency> chunks;
  std::size_t resident = 0;
  std::size_t evicted = 0;
  // Level changes between resident states, excluding arrival and eviction.
  std::size_t promoted = 0;
  std::size_t demoted = 0;
  // Exact seam-safe surface topology budget, excluding optional skirts.
  [[nodiscard]] std::size_t triangles() const;
};

// Evaluates a new policy state without loading resources or mutating the world.
// Chunks outside the local view distance and every gameplay radius are eviction
// candidates. Resident levels follow camera distance and geometric error,
// clamped to minimumResidentLod. Promotions are immediate; continuous coarser
// demand and eviction wait hysteresisSeconds.
//
// Rejects nonfinite inputs, negative dt/viewDistance/grid extents, invalid LOD
// levels, and chunk lists that do not tile the grid. Negative finite radius and
// hysteresis normalize to zero. An empty grid returns an empty state.
// Previous entries are reused only when their index and rectangle match.
//
// This allocation-bearing reference evaluator validates tiling and evaluates
// geometric error on each call. It is not a prepared per-frame streaming path.
[[nodiscard]] TerrainStreamingState
updateTerrainStreaming(const TerrainStreamingState &previous,
                       const HeightField &field, Vec2 cameraPosition,
                       float viewDistance, float dt,
                       const TerrainStreamingRelevance &relevance);

// Queries one chunk using terrain-local camera x/z and gameplay anchors.
// Applies eviction delay using outsideForSeconds, but has no previous held
// level with which to apply demotion delay. Negative finite elapsed time
// normalizes to zero. Shares input validation with the whole-state evaluator;
// only the whole-state evaluator validates complete tiling.
[[nodiscard]] std::optional<TerrainLod>
terrainChunkTargetLevel(const HeightField &field, const TerrainChunk &chunk,
                        Vec2 cameraPosition, float viewDistance,
                        const TerrainStreamingRelevance &relevance,
                        float outsideForSeconds);
} // namespace demi::runtime
