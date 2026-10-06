#include "demi/runtime/terrain/TerrainStreaming.h"

#include "demi/runtime/terrain/TerrainGenerator.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace demi::runtime {
namespace {

// The chunk's footprint in world units. Two samples are enough: the field is a
// regular grid, so the corners bound every point between them.
struct ChunkRect {
  Vec2 first;
  Vec2 last;
};

ChunkRect rectOf(const HeightField &field, const TerrainChunk &chunk) {
  return {field.position(chunk.firstCellX, chunk.firstCellZ),
          field.position(chunk.firstCellX + chunk.cellsX, chunk.firstCellZ + chunk.cellsZ)};
}

// Distance to the NEAREST point of a footprint, never to its centre, and this is
// the same rule TerrainLod measures with: a chunk is visible well before its
// centre is close, and using the centre makes the near seam pop. Keeping the two
// identical is what makes the level streamed and the level built the same
// decision rather than two nearly-equal ones.
float distanceToRect(const ChunkRect &rect, Vec2 point) {
  const float dx = std::max({0.F, rect.first.x - point.x, point.x - rect.last.x});
  const float dz = std::max({0.F, rect.first.y - point.y, point.y - rect.last.y});
  return std::hypot(dx, dz);
}

// Everything about one chunk that both entry points have to agree on.
struct ChunkDemand {
  float cameraDistance = 0.F;
  float anchorDistance = std::numeric_limits<float>::infinity();
  bool gameplayHeld = false;
  // Outside the view distance AND outside every anchor: the only eviction
  // candidate, and the two tests are ANDed deliberately. A chunk gameplay can
  // reach stays resident however far the camera is, and a chunk the camera can
  // see is not evicted merely for being far from every anchor.
  bool unsatisfied = false;
};

ChunkDemand demandFor(const HeightField &field, const TerrainChunk &chunk,
                      Vec2 cameraPosition, float viewDistance,
                      const TerrainStreamingRelevance &relevance) {
  const ChunkRect rect = rectOf(field, chunk);
  ChunkDemand demand;
  demand.cameraDistance = distanceToRect(rect, cameraPosition);
  // A negative radius is normalised to zero, which is the documented "camera
  // only" case, so the held test cannot be reached by a nonsensical radius.
  const float radius = std::max(0.F, relevance.gameplayRadius);
  if (radius > 0.F) {
    for (const Vec3 anchor : relevance.gameplayAnchors) {
      demand.anchorDistance =
          std::min(demand.anchorDistance,
                   distanceToRect(rect, Vec2{anchor.x, anchor.z}));
    }
    demand.gameplayHeld = demand.anchorDistance <= radius;
  }
  demand.unsatisfied = !demand.gameplayHeld && demand.cameraDistance > viewDistance;
  return demand;
}

// The coarsest level a resident chunk may hold, as an ordinal.
//
// `Off` is not a resident level: it is the absence of a mesh. Allowing it as a
// floor would make "resident" and "has geometry" disagree, so a floor of `Off`
// is read as `Coarse`, the coarsest level that still builds one.
int residentFloorIndex(TerrainLod floor) {
  return std::min(terrainLodIndex(floor), terrainLodIndex(TerrainLod::Coarse));
}

// What the selector asked for, and what that answer is allowed to become.
//
// Both are needed: the level decides residency and the mesh, while the raw
// selection is what lets a reason name the floor as the cause instead of blaming
// the camera for a level the author configured.
struct ChunkLevel {
  TerrainLod selected = TerrainLod::Full;
  TerrainLod level = TerrainLod::Full;
  bool floored = false;
};

// The level for one chunk: the LOD selector's answer for its nearest distance,
// never coarser than the resident floor. This is the ONLY place a chunk's level
// is decided, so the streamed level and the mesh a renderer builds from it cannot
// disagree.
//
// The anchor deliberately does not appear here. An anchor decides residency and
// the floor; the level is always the camera's distance through the same selector,
// because a second distance path is a second opinion about detail.
ChunkLevel levelFor(const HeightField &field, const TerrainChunk &chunk,
                    float cameraDistance, const TerrainStreamingRelevance &relevance) {
  ChunkLevel result;
  result.selected = terrainLodForChunk(field, chunk, cameraDistance, relevance.lod);
  const int floorIndex = residentFloorIndex(relevance.minimumResidentLod);
  result.level = TerrainLod(std::min(terrainLodIndex(result.selected), floorIndex));
  result.floored = terrainLodIndex(result.selected) > floorIndex;
  return result;
}
// One decimal, without an ostream. Only used to explain a decision to a human, so
// the exact shortest representation is not worth a dependency.
std::string metres(float value) {
  char buffer[32];
  const auto formatted =
      std::to_chars(buffer, buffer + sizeof(buffer), static_cast<double>(value),
                    std::chars_format::fixed, 1);
  return std::string(buffer, formatted.ptr);
}

// Every reason names the level it settled on, so an overlay can be checked by a
// test rather than trusted, and every reason names what is keeping the chunk
// alive. A reason that cannot name its own cause is indistinguishable from a bug.
//
// The clause is chosen by what is actually holding the chunk, not by which input
// happened to be tested first: a gameplay-held chunk that is being kept finer
// than the camera wants says so and says why, rather than blaming the camera for
// a level the anchor imposed.
std::string reasonFor(const ChunkDemand &demand, float viewDistance,
                      float outsideForSeconds, float delay, const ChunkLevel &wanted,
                      TerrainLod settled, bool evicted, float coarserForSeconds) {
  const int settledIndex = terrainLodIndex(settled);
  const int wantedIndex = terrainLodIndex(wanted.selected);
  const bool heldFiner = settledIndex < wantedIndex;
  std::string reason;
  if (evicted) {
    reason = "evicted: unsatisfied by " + metres(demand.cameraDistance) +
             " past view distance " + metres(viewDistance) + " for " +
             metres(outsideForSeconds) + "s of " + metres(delay) + "s";
  } else if (demand.gameplayHeld) {
    reason = std::string("gameplay anchor at ") + metres(demand.anchorDistance) +
             " keeps it resident; camera " + metres(demand.cameraDistance) +
             " past view distance " + metres(viewDistance);
    // The floor, not the delay, when the floor is what raised the level. Naming
    // the wrong one of those would send an author looking at the camera.
    if (wanted.floored && heldFiner)
      reason += ", floored from " + std::string(terrainLodName(wanted.selected));
    else if (heldFiner)
      reason += ", kept for now; camera wants " +
                std::string(terrainLodName(wanted.selected)) + " for " +
                metres(coarserForSeconds) + "s of " + metres(delay) + "s";
  } else if (demand.unsatisfied) {
    reason = "unsatisfied: " + metres(demand.cameraDistance) +
             " past view distance " + metres(viewDistance) + " for " +
             metres(outsideForSeconds) + "s of " + metres(delay) + "s, still resident";
  } else if (heldFiner) {
    reason = "camera " + metres(demand.cameraDistance) + " within view distance " +
             metres(viewDistance) + ", wants a coarser level for " +
             metres(coarserForSeconds) + "s of " + metres(delay) + "s";
  } else {
    reason = "camera " + metres(demand.cameraDistance) + " within view distance " +
             metres(viewDistance);
  }
  reason += "; level ";
  reason += terrainLodName(settled);
  return reason;
}

void requirePositiveGrid(const HeightField &field) {
  if (field.cellsX <= 0 || field.cellsZ <= 0)
    throw std::invalid_argument(
        "Terrain streaming cannot stream a field with an empty grid");
}

void requireFiniteCamera(Vec2 cameraPosition) {
  if (!std::isfinite(cameraPosition.x) || !std::isfinite(cameraPosition.y))
    throw std::invalid_argument(
        "Terrain streaming camera position is not finite");
}

void requireFiniteAnchors(const TerrainStreamingRelevance &relevance) {
  for (const Vec3 anchor : relevance.gameplayAnchors)
    if (!std::isfinite(anchor.x) || !std::isfinite(anchor.z))
      throw std::invalid_argument(
          "Terrain streaming gameplay anchor is not finite");
}

void requireViewDistance(float viewDistance) {
  if (!std::isfinite(viewDistance))
    throw std::invalid_argument("Terrain streaming view distance is not finite");
  if (viewDistance < 0.F)
    throw std::invalid_argument("Terrain streaming view distance is negative");
}

void requireChunkFits(const HeightField &field, const TerrainChunk &chunk) {
  if (chunk.cellsX <= 0 || chunk.cellsZ <= 0 || chunk.firstCellX < 0 ||
      chunk.firstCellZ < 0 || chunk.firstCellX > field.cellsX - chunk.cellsX ||
      chunk.firstCellZ > field.cellsZ - chunk.cellsZ)
    throw std::invalid_argument(
        "Terrain streaming chunk rectangle does not fit inside the heightfield");
}

// A chunk list is a CLAIM that the field is tiled, not merely a list of
// rectangles. Streaming resolves every cell to the chunk that owns it, so a gap
// or an overlap would leave a part of the field unstreamed or streamed twice, and
// the state would quietly disagree with the mesh.
//
// The proof is total area plus disjointness, both of which are cheap:
//   - every rectangle inside the grid, checked above;
//   - summed area exactly equal to the grid, so no cell is left uncovered once no
//     two rectangles claim the same one;
//   - no two rectangles overlap. Sorted by firstCellX, a later rectangle can only
//     overlap while its own firstCellX is still inside an earlier one's x span,
//     so the scan stops as soon as it passes that, which is a handful of tests per
//     rectangle for any regular tiling and never walks the grid.
// A per-cell bitmap would be simpler and would cost a 512x512 field a quarter of a
// million writes on EVERY update, which is the wrong place to spend that.
void requireChunkTiling(const HeightField &field) {
  // An empty list is not a broken tiling: TerrainLod already reads a field with no
  // chunk bookkeeping as one chunk covering its own extent, and streaming resolves
  // the same single chunk so the two agree on what is being drawn.
  if (field.chunks.empty())
    return;
  std::uint64_t covered = 0;
  for (std::size_t index = 0; index < field.chunks.size(); ++index) {
    const TerrainChunk &chunk = field.chunks[index];
    if (chunk.cellsX <= 0 || chunk.cellsZ <= 0 || chunk.firstCellX < 0 ||
        chunk.firstCellZ < 0 || chunk.firstCellX > field.cellsX - chunk.cellsX ||
        chunk.firstCellZ > field.cellsZ - chunk.cellsZ)
      throw std::invalid_argument("Terrain streaming chunk " + std::to_string(index) +
                                  " falls outside the heightfield grid");
    covered += std::uint64_t(chunk.cellsX) * std::uint64_t(chunk.cellsZ);
  }
  const std::uint64_t grid =
      std::uint64_t(field.cellsX) * std::uint64_t(field.cellsZ);
  if (covered != grid)
    throw std::invalid_argument(
        "Terrain streaming chunk list does not tile the heightfield grid: it "
        "covers " +
        std::to_string(covered) + " of " + std::to_string(grid) + " cells");

  std::vector<std::size_t> order(field.chunks.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::sort(order.begin(), order.end(), [&field](std::size_t a, std::size_t b) {
    if (field.chunks[a].firstCellX != field.chunks[b].firstCellX)
      return field.chunks[a].firstCellX < field.chunks[b].firstCellX;
    return field.chunks[a].firstCellZ < field.chunks[b].firstCellZ;
  });
  for (std::size_t left = 0; left < order.size(); ++left) {
    const TerrainChunk &a = field.chunks[order[left]];
    for (std::size_t right = left + 1; right < order.size(); ++right) {
      const TerrainChunk &b = field.chunks[order[right]];
      // Sorted by firstCellX, so once b starts past a's x span the two cannot
      // share a cell and neither can anything after b.
      if (b.firstCellX >= a.firstCellX + a.cellsX)
        break;
      if (b.firstCellZ < a.firstCellZ + a.cellsZ &&
          a.firstCellZ < b.firstCellZ + b.cellsZ)
        throw std::invalid_argument("Terrain streaming chunk list does not tile "
                                    "the heightfield grid: chunks " +
                                    std::to_string(order[left]) + " and " +
                                    std::to_string(order[right]) + " overlap");
    }
  }
}

// The chunks this update decides about, in the field's own order.
std::vector<TerrainChunk> chunksOf(const HeightField &field) {
  if (!field.chunks.empty())
    return field.chunks;
  return {TerrainChunk{0, 0, field.cellsX, field.cellsZ}};
}

// A previous entry for this chunk, or nothing. Position AND rectangle must both
// match, so a state left over from a different field is ignored rather than
// applied to the wrong rectangle, and a caller that streams two terrains with one
// state object cannot have them bleed into each other.
const TerrainChunkResidency *previousEntry(const TerrainStreamingState &previous,
                                           std::size_t index, const TerrainChunk &chunk) {
  if (index >= previous.chunks.size())
    return nullptr;
  const TerrainChunkResidency &candidate = previous.chunks[index];
  if (candidate.chunkIndex != index || candidate.firstCellX != chunk.firstCellX ||
      candidate.firstCellZ != chunk.firstCellZ || candidate.cellsX != chunk.cellsX ||
      candidate.cellsZ != chunk.cellsZ)
    return nullptr;
  return &candidate;
}

// Vertices along one axis of a chunk at one level: the first cell, every `step`
// cells after it, and the chunk's exact far edge when that edge is not a whole
// number of strides away. Identical to the lattice buildTerrainLodMesh emits, so
// the budget and the mesh cannot drift apart.
std::size_t latticeSize(int cells, int step) {
  if (cells <= 0 || step <= 0)
    return 0;
  const auto uniform = std::size_t(cells / step) + 1;
  return uniform + (cells % step != 0 ? 1 : 0);
}

} // namespace

std::size_t TerrainStreamingState::triangles() const {
  std::size_t total = 0;
  for (const TerrainChunkResidency &chunk : chunks) {
    // An evicted chunk has no mesh, so it costs nothing. Counting it would make a
    // budget grow as the camera pulls away from a region.
    if (!chunk.resident)
      continue;
    const int step = terrainLodStep(chunk.level);
    const std::size_t columns = latticeSize(chunk.cellsX, step);
    const std::size_t rows = latticeSize(chunk.cellsZ, step);
    if (columns < 2 || rows < 2)
      continue;
    total += (columns - 1) * (rows - 1) * 2;
  }
  return total;
}

std::optional<TerrainLod>
terrainChunkTargetLevel(const HeightField &field, const TerrainChunk &chunk,
                        Vec2 cameraPosition, float viewDistance,
                        const TerrainStreamingRelevance &relevance,
                        float outsideForSeconds) {
  requirePositiveGrid(field);
  requireFiniteCamera(cameraPosition);
  requireViewDistance(viewDistance);
  requireChunkFits(field, chunk);

  const ChunkDemand demand =
      demandFor(field, chunk, cameraPosition, viewDistance, relevance);
  // The delay the caller reports is the delay it has already served, so a caller
  // polling this instead of advancing a state still learns when an eviction
  // becomes due rather than being told to evict a chunk that is still waiting.
  const float delay = std::max(0.F, relevance.hysteresisSeconds);
  if (demand.unsatisfied && std::max(0.F, outsideForSeconds) >= delay)
    return std::nullopt;
  return levelFor(field, chunk, demand.cameraDistance, relevance).level;
}

TerrainStreamingState
updateTerrainStreaming(const TerrainStreamingState &previous,
                       const HeightField &field, Vec2 cameraPosition,
                       float viewDistance, float dt,
                       const TerrainStreamingRelevance &relevance) {
  if (!std::isfinite(dt))
    throw std::invalid_argument("Terrain streaming time step is not finite");
  if (dt < 0.F)
    throw std::invalid_argument("Terrain streaming time step is negative");
  if (field.cellsX < 0 || field.cellsZ < 0)
    throw std::invalid_argument(
        "Terrain streaming heightfield has a negative grid extent");
  requireViewDistance(viewDistance);
  requireFiniteCamera(cameraPosition);
  requireFiniteAnchors(relevance);
  requireChunkTiling(field);

  // An empty grid has nothing to stream and is not an error: a caller asking to
  // stream a terrain that generated no cells gets an empty state back.
  TerrainStreamingState next;
  if (field.cellsX == 0 || field.cellsZ == 0)
    return next;

  const std::vector<TerrainChunk> chunks = chunksOf(field);
  const float delay = std::max(0.F, relevance.hysteresisSeconds);
  next.chunks.resize(chunks.size());

  for (std::size_t index = 0; index < chunks.size(); ++index) {
    const TerrainChunk &chunk = chunks[index];
    requireChunkFits(field, chunk);
    const ChunkDemand demand =
        demandFor(field, chunk, cameraPosition, viewDistance, relevance);
    const TerrainChunkResidency *before = previousEntry(previous, index, chunk);

    // The clocks. Both advance by this update's dt and both reset the instant
    // anything wants the chunk again, so a camera that crosses a boundary and
    // comes straight back never banks any delay towards evicting or blurring it.
    const float carriedOutside =
        before != nullptr ? std::max(0.F, before->outsideForSeconds) : 0.F;
    const float outsideFor = demand.unsatisfied ? carriedOutside + dt : 0.F;

    // THE RESIDENCY RULE. Unsatisfied for the full delay is the only way out;
    // being gameplay-held is the only way to be exempt.
    const bool evict = demand.unsatisfied && outsideFor >= delay;

    // Wanted regardless: a chunk still inside the eviction delay has a level, and
    // an evicted chunk still has to be compared against the level it was dropped
    // from, so the level is decided before the eviction is applied.
    const ChunkLevel wanted =
        levelFor(field, chunk, demand.cameraDistance, relevance);

    TerrainChunkResidency &entry = next.chunks[index];
    entry.chunkIndex = index;
    entry.firstCellX = chunk.firstCellX;
    entry.firstCellZ = chunk.firstCellZ;
    entry.cellsX = chunk.cellsX;
    entry.cellsZ = chunk.cellsZ;
    entry.gameplayHeld = demand.gameplayHeld;
    entry.outsideForSeconds = outsideFor;
    entry.level = TerrainLod::Off;

    if (evict) {
      entry.resident = false;
      ++next.evicted;
      entry.reason = reasonFor(demand, viewDistance, outsideFor, delay, wanted,
                               entry.level, true, 0.F);
      continue;
    }
    ++next.resident;
    entry.resident = true;

    // A chunk the previous state does not describe adopts its wanted level now.
    // There is no resident mesh to protect, so there is no transition to slow
    // down, and a freshly loaded field must not spend its first seconds at Full
    // before it is allowed to settle.
    if (before == nullptr || !before->resident) {
      entry.level = wanted.level;
      entry.reason = reasonFor(demand, viewDistance, outsideFor, delay, wanted,
                               wanted.level, false, 0.F);
      continue;
    }

    // The asymmetry, on every chunk that is changing level:
    //
    //   FINER  -> this update, always. Something came closer or the camera turned
    //             towards it, and the player is about to look at the difference.
    //   COARSER -> only after the chunk has asked for it continuously for the
    //             full delay. A camera rotating in place crosses a threshold for
    //             a single frame and comes straight back; without this a chunk on
    //             that threshold would rebuild itself every time the camera
    //             orbited it, which is a far more visible artefact than a chunk
    //             that stays slightly too detailed for three quarters of a
    //             second.
    const int wantedIndex = terrainLodIndex(wanted.level);
    const int heldIndex = terrainLodIndex(before->level);
    if (wantedIndex < heldIndex) {
      entry.level = wanted.level;
      entry.coarserForSeconds = 0.F;
      ++next.promoted;
    } else if (wantedIndex > heldIndex) {
      const float coarserFor = before->coarserForSeconds + dt;
      if (coarserFor >= delay) {
        entry.level = wanted.level;
        entry.coarserForSeconds = 0.F;
        ++next.demoted;
      } else {
        // Hold the finer level. A chunk can never be finer than Full, so this
        // cannot run off the end of the table: Full is always index 0.
        entry.level = before->level;
        entry.coarserForSeconds = coarserFor;
      }
    } else {
      entry.level = before->level;
      entry.coarserForSeconds = 0.F;
    }
    entry.reason = reasonFor(demand, viewDistance, outsideFor, delay, wanted,
                             entry.level, false, entry.coarserForSeconds);
  }
  return next;
}
} // namespace demi::runtime
