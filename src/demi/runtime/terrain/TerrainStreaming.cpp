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

// Local x/z footprint of a regular heightfield chunk.
struct ChunkRect {
  Vec2 first;
  Vec2 last;
};

ChunkRect rectOf(const HeightField &field, const TerrainChunk &chunk) {
  return {field.position(chunk.firstCellX, chunk.firstCellZ),
          field.position(chunk.firstCellX + chunk.cellsX,
                         chunk.firstCellZ + chunk.cellsZ)};
}

// Use the nearest footprint point, matching the LOD selector.
float distanceToRect(const ChunkRect &rect, Vec2 point) {
  const float dx =
      std::max({0.F, rect.first.x - point.x, point.x - rect.last.x});
  const float dz =
      std::max({0.F, rect.first.y - point.y, point.y - rect.last.y});
  return std::hypot(dx, dz);
}

// Everything about one chunk that both entry points have to agree on.
struct ChunkDemand {
  float cameraDistance = 0.F;
  float anchorDistance = std::numeric_limits<float>::infinity();
  bool gameplayHeld = false;
  // Eviction requires being outside both camera and gameplay radii.
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
  demand.unsatisfied =
      !demand.gameplayHeld && demand.cameraDistance > viewDistance;
  return demand;
}

// Off has no mesh; a resident floor of Off therefore means Coarse.
int residentFloorIndex(TerrainLod floor) {
  return std::min(terrainLodIndex(floor), terrainLodIndex(TerrainLod::Coarse));
}

// Keep the unclamped selection so diagnostics can explain the resident floor.
struct ChunkLevel {
  TerrainLod selected = TerrainLod::Full;
  TerrainLod level = TerrainLod::Full;
  bool floored = false;
};

// Camera distance sets detail; gameplay anchors affect residency only.
ChunkLevel levelFor(const HeightField &field, const TerrainChunk &chunk,
                    float cameraDistance,
                    const TerrainStreamingRelevance &relevance) {
  ChunkLevel result;
  result.selected =
      terrainLodForChunk(field, chunk, cameraDistance, relevance.lod);
  const int floorIndex = residentFloorIndex(relevance.minimumResidentLod);
  result.floored = terrainLodIndex(result.selected) > floorIndex;
  result.level = result.selected;
  if (result.floored) {
    // Apply the resident limit before checking geometric error. Off has no
    // geometry, so merely replacing it with the floor would bypass that check.
    auto residentSettings = relevance.lod;
    residentSettings.maximumLevel =
        std::min(terrainLodUsableLevels(relevance.lod), floorIndex);
    result.level =
        terrainLodForChunk(field, chunk, cameraDistance, residentSettings);
  }
  return result;
}
// One decimal, without an ostream. Only used to explain a decision to a human,
// so the exact shortest representation is not worth a dependency.
std::string metres(float value) {
  char buffer[32];
  const auto formatted =
      std::to_chars(buffer, buffer + sizeof(buffer), static_cast<double>(value),
                    std::chars_format::fixed, 1);
  return std::string(buffer, formatted.ptr);
}

// Explain residency and the settled level, including delayed transitions.
std::string reasonFor(const ChunkDemand &demand, float viewDistance,
                      float outsideForSeconds, float delay,
                      const ChunkLevel &wanted, TerrainLod settled,
                      bool evicted, float coarserForSeconds) {
  const int settledIndex = terrainLodIndex(settled);
  const int wantedIndex = terrainLodIndex(wanted.selected);
  const bool heldFiner = settledIndex < wantedIndex;
  std::string reason;
  if (evicted) {
    reason = "evicted: unsatisfied by " + metres(demand.cameraDistance) +
             " past view distance " + metres(viewDistance) + " for " +
             metres(outsideForSeconds) + "s of " + metres(delay) + "s";
  } else if (demand.gameplayHeld) {
    reason =
        std::string("gameplay anchor at ") + metres(demand.anchorDistance) +
        " keeps it resident; camera distance " + metres(demand.cameraDistance) +
        ", view distance " + metres(viewDistance);
    // Distinguish the resident limit from delayed coarsening.
    if (wanted.floored && heldFiner) {
      reason +=
          ", floored from " + std::string(terrainLodName(wanted.selected));
    } else if (heldFiner) {
      reason += ", kept for now; camera wants " +
                std::string(terrainLodName(wanted.selected)) + " for " +
                metres(coarserForSeconds) + "s of " + metres(delay) + "s";
    }
  } else if (demand.unsatisfied) {
    reason = "unsatisfied: " + metres(demand.cameraDistance) +
             " past view distance " + metres(viewDistance) + " for " +
             metres(outsideForSeconds) + "s of " + metres(delay) +
             "s, still resident";
  } else if (heldFiner) {
    reason = "camera " + metres(demand.cameraDistance) +
             " within view distance " + metres(viewDistance) +
             ", wants a coarser level for " + metres(coarserForSeconds) +
             "s of " + metres(delay) + "s";
  } else {
    reason = "camera " + metres(demand.cameraDistance) +
             " within view distance " + metres(viewDistance);
  }
  reason += "; level ";
  reason += terrainLodName(settled);
  return reason;
}

void requirePositiveGrid(const HeightField &field) {
  if (field.cellsX <= 0 || field.cellsZ <= 0) {
    throw std::invalid_argument(
        "Terrain streaming cannot stream a field with an empty grid");
  }
}

void requireFiniteCamera(Vec2 cameraPosition) {
  if (!std::isfinite(cameraPosition.x) || !std::isfinite(cameraPosition.y)) {
    throw std::invalid_argument(
        "Terrain streaming camera position is not finite");
  }
}

void requireFiniteAnchors(const TerrainStreamingRelevance &relevance) {
  for (const Vec3 anchor : relevance.gameplayAnchors) {
    if (!std::isfinite(anchor.x) || !std::isfinite(anchor.y) ||
        !std::isfinite(anchor.z)) {
      throw std::invalid_argument(
          "Terrain streaming gameplay anchor is not finite");
    }
  }
}

void requireViewDistance(float viewDistance) {
  if (!std::isfinite(viewDistance)) {
    throw std::invalid_argument(
        "Terrain streaming view distance is not finite");
  }
  if (viewDistance < 0.F) {
    throw std::invalid_argument("Terrain streaming view distance is negative");
  }
}

void requireChunkFits(const HeightField &field, const TerrainChunk &chunk) {
  if (chunk.cellsX <= 0 || chunk.cellsZ <= 0 || chunk.firstCellX < 0 ||
      chunk.firstCellZ < 0 || chunk.firstCellX > field.cellsX - chunk.cellsX ||
      chunk.firstCellZ > field.cellsZ - chunk.cellsZ) {
    throw std::invalid_argument("Terrain streaming chunk rectangle does not "
                                "fit inside the heightfield");
  }
}

// Verify complete coverage by checking bounds, total area, and rectangle
// overlap.
void requireChunkTiling(const HeightField &field) {
  // An unchunked field is evaluated as one rectangle covering its extent.
  if (field.chunks.empty()) {
    return;
  }
  std::uint64_t covered = 0;
  for (std::size_t index = 0; index < field.chunks.size(); ++index) {
    const TerrainChunk &chunk = field.chunks[index];
    if (chunk.cellsX <= 0 || chunk.cellsZ <= 0 || chunk.firstCellX < 0 ||
        chunk.firstCellZ < 0 ||
        chunk.firstCellX > field.cellsX - chunk.cellsX ||
        chunk.firstCellZ > field.cellsZ - chunk.cellsZ) {
      throw std::invalid_argument("Terrain streaming chunk " +
                                  std::to_string(index) +
                                  " falls outside the heightfield grid");
    }
    covered += std::uint64_t(chunk.cellsX) * std::uint64_t(chunk.cellsZ);
  }
  const std::uint64_t grid =
      std::uint64_t(field.cellsX) * std::uint64_t(field.cellsZ);
  if (covered != grid) {
    throw std::invalid_argument(
        "Terrain streaming chunk list does not tile the heightfield grid: it "
        "covers " +
        std::to_string(covered) + " of " + std::to_string(grid) + " cells");
  }

  std::vector<std::size_t> order(field.chunks.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::sort(order.begin(), order.end(), [&field](std::size_t a, std::size_t b) {
    if (field.chunks[a].firstCellX != field.chunks[b].firstCellX) {
      return field.chunks[a].firstCellX < field.chunks[b].firstCellX;
    }
    return field.chunks[a].firstCellZ < field.chunks[b].firstCellZ;
  });
  for (std::size_t left = 0; left < order.size(); ++left) {
    const TerrainChunk &a = field.chunks[order[left]];
    for (std::size_t right = left + 1; right < order.size(); ++right) {
      const TerrainChunk &b = field.chunks[order[right]];
      // Sorted by firstCellX, so once b starts past a's x span the two cannot
      // share a cell and neither can anything after b.
      if (b.firstCellX >= a.firstCellX + a.cellsX) {
        break;
      }
      if (b.firstCellZ < a.firstCellZ + a.cellsZ &&
          a.firstCellZ < b.firstCellZ + b.cellsZ) {
        throw std::invalid_argument(
            "Terrain streaming chunk list does not tile "
            "the heightfield grid: chunks " +
            std::to_string(order[left]) + " and " +
            std::to_string(order[right]) + " overlap");
      }
    }
  }
}

// The chunks this update decides about, in the field's own order.
std::vector<TerrainChunk> chunksOf(const HeightField &field) {
  if (!field.chunks.empty()) {
    return field.chunks;
  }
  return {TerrainChunk{0, 0, field.cellsX, field.cellsZ}};
}

// Reuse prior state only when its slot and chunk rectangle still agree.
const TerrainChunkResidency *
previousEntry(const TerrainStreamingState &previous, std::size_t index,
              const TerrainChunk &chunk) {
  if (index >= previous.chunks.size()) {
    return nullptr;
  }
  const TerrainChunkResidency &candidate = previous.chunks[index];
  if (candidate.chunkIndex != index ||
      candidate.firstCellX != chunk.firstCellX ||
      candidate.firstCellZ != chunk.firstCellZ ||
      candidate.cellsX != chunk.cellsX || candidate.cellsZ != chunk.cellsZ) {
    return nullptr;
  }
  return &candidate;
}

void requireFiniteRelevance(const TerrainStreamingRelevance &relevance) {
  requireFiniteAnchors(relevance);
  if (!std::isfinite(relevance.gameplayRadius)) {
    throw std::invalid_argument(
        "Terrain streaming gameplay radius is not finite");
  }
  if (!std::isfinite(relevance.hysteresisSeconds)) {
    throw std::invalid_argument("Terrain streaming hysteresis is not finite");
  }
  const int floor = terrainLodIndex(relevance.minimumResidentLod);
  if (floor < 0 || floor > terrainLodIndex(TerrainLod::Off)) {
    throw std::invalid_argument("Terrain streaming resident LOD is invalid");
  }
  for (const float threshold : relevance.lod.distanceThresholds) {
    if (!std::isfinite(threshold)) {
      throw std::invalid_argument(
          "Terrain streaming LOD threshold is not finite");
    }
  }
  if (!std::isfinite(relevance.lod.geometricError)) {
    throw std::invalid_argument(
        "Terrain streaming geometric error is not finite");
  }
}

void requireFiniteFieldSize(const HeightField &field) {
  if (!std::isfinite(field.size.x) || !std::isfinite(field.size.y) ||
      field.size.x <= 0.0F || field.size.y <= 0.0F) {
    throw std::invalid_argument(
        "Terrain streaming field size must be finite and positive");
  }
}

} // namespace

std::size_t TerrainStreamingState::triangles() const {
  std::size_t total = 0;
  for (const TerrainChunkResidency &chunk : chunks) {
    // An evicted chunk has no mesh, so it costs nothing. Counting it would make
    // a budget grow as the camera pulls away from a region.
    if (!chunk.resident) {
      continue;
    }
    total +=
        terrainLodSurfaceTriangleCount(chunk.cellsX, chunk.cellsZ, chunk.level);
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
  requireFiniteRelevance(relevance);
  requireFiniteFieldSize(field);
  requireChunkFits(field, chunk);
  if (!std::isfinite(outsideForSeconds)) {
    throw std::invalid_argument(
        "Terrain streaming outside duration is not finite");
  }

  const ChunkDemand demand =
      demandFor(field, chunk, cameraPosition, viewDistance, relevance);
  // The elapsed duration is supplied by the caller; this query does not advance
  // it.
  const float delay = std::max(0.F, relevance.hysteresisSeconds);
  if (demand.unsatisfied && std::max(0.F, outsideForSeconds) >= delay) {
    return std::nullopt;
  }
  return levelFor(field, chunk, demand.cameraDistance, relevance).level;
}

TerrainStreamingState
updateTerrainStreaming(const TerrainStreamingState &previous,
                       const HeightField &field, Vec2 cameraPosition,
                       float viewDistance, float dt,
                       const TerrainStreamingRelevance &relevance) {
  if (!std::isfinite(dt)) {
    throw std::invalid_argument("Terrain streaming time step is not finite");
  }
  if (dt < 0.F) {
    throw std::invalid_argument("Terrain streaming time step is negative");
  }
  if (field.cellsX < 0 || field.cellsZ < 0) {
    throw std::invalid_argument(
        "Terrain streaming heightfield has a negative grid extent");
  }
  requireViewDistance(viewDistance);
  requireFiniteCamera(cameraPosition);
  requireFiniteRelevance(relevance);
  requireChunkTiling(field);

  // An empty grid has nothing to stream and is not an error: a caller asking to
  // stream a terrain that generated no cells gets an empty state back.
  TerrainStreamingState next;
  if (field.cellsX == 0 || field.cellsZ == 0) {
    return next;
  }

  requireFiniteFieldSize(field);
  const std::vector<TerrainChunk> chunks = chunksOf(field);
  const float delay = std::max(0.F, relevance.hysteresisSeconds);
  next.chunks.resize(chunks.size());

  for (std::size_t index = 0; index < chunks.size(); ++index) {
    const TerrainChunk &chunk = chunks[index];
    requireChunkFits(field, chunk);
    const ChunkDemand demand =
        demandFor(field, chunk, cameraPosition, viewDistance, relevance);
    const TerrainChunkResidency *before = previousEntry(previous, index, chunk);

    // Returning within either radius resets only the eviction clock.
    // The demotion clock resets when coarser demand stops below.
    const float carriedOutside =
        before != nullptr ? std::max(0.F, before->outsideForSeconds) : 0.F;
    const float outsideFor = demand.unsatisfied ? carriedOutside + dt : 0.F;

    // Evict only after continuous unsatisfied demand reaches the delay.
    const bool evict = demand.unsatisfied && outsideFor >= delay;

    // Retain the wanted level for diagnostics even when this update evicts.
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

    // Arrival has no existing resident level whose transition needs a delay.
    if (before == nullptr || !before->resident) {
      entry.level = wanted.level;
      entry.reason = reasonFor(demand, viewDistance, outsideFor, delay, wanted,
                               wanted.level, false, 0.F);
      continue;
    }

    // Promote immediately; delay continuous demand for any coarser level.
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
        // Hold the finer level until continuous coarser demand reaches the
        // delay.
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
