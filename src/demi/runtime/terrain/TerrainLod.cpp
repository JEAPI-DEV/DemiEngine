#include "demi/runtime/terrain/TerrainLod.h"

#include "demi/runtime/terrain/TerrainGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace demi::runtime {
namespace {

// A chunk dimension reduced to one level's sample positions.
//
// Samples land on the chunk's first cell, then every `step` cells after it, and
// the chunk's exact far edge is always included even when it is not a whole
// number of steps away. That trailing sample is what keeps two adjacent chunks
// agreeing on their shared edge: both read the same heightfield sample at the
// boundary cell, whichever level each one is at.
struct Lattice {
  int count = 0;
  int step = 1;
  int uniform = 1; // entries at 0, step, 2 * step, ... below `count`

  std::size_t size() const {
    if (count <= 0)
      return 0;
    return std::size_t(uniform) + (std::size_t(uniform - 1) * std::size_t(step) ==
                                           std::size_t(count)
                                       ? 0
                                       : 1);
  }
  int sample(std::size_t entry) const {
    return entry < std::size_t(uniform) ? int(entry) * step : count;
  }
  // The lattice entry nearest a cell offset, in O(1). Everything at or past the
  // last regular step belongs to the exact far edge.
  int nearestEntry(int offset) const {
    if (offset >= (uniform - 1) * step)
      return int(size()) - 1;
    return offset / step;
  }
};

Lattice makeLattice(int count, int step) {
  Lattice lattice;
  lattice.count = std::max(0, count);
  lattice.step = std::max(1, step);
  lattice.uniform = lattice.count / lattice.step + 1;
  return lattice;
}

int stepForIndex(int index) {
  if (index < 0 || index > terrainLodLevelCount - 1)
    return 0;
  return terrainLodStep(TerrainLod(index));
}

// Peak height distance between the full-resolution surface and the surface the
// reduced lattice implies, over one cell rectangle. This is the LOD geometric
// error, measured against the field's own samples rather than an assumed
// approximation of them.
//
// O(cells in the rectangle), which is always one chunk's cells.
float deviationIn(const HeightField &field, int step, int firstCellX, int firstCellZ,
                  int cellsX, int cellsZ) {
  if (step <= 1)
    return 0.F;
  const auto latticeX = makeLattice(cellsX, step);
  const auto latticeZ = makeLattice(cellsZ, step);
  float worst = 0.F;
  for (int z = 0; z <= cellsZ; ++z) {
    const int reducedZ = firstCellZ + latticeZ.sample(
                                       std::size_t(std::max(0, latticeZ.nearestEntry(z))));
    const int fullZ = firstCellZ + z;
    for (int x = 0; x <= cellsX; ++x) {
      const int reducedX =
          firstCellX +
          latticeX.sample(std::size_t(std::max(0, latticeX.nearestEntry(x))));
      const float full = field.height(firstCellX + x, fullZ);
      worst = std::max(worst, std::abs(field.height(reducedX, reducedZ) - full));
    }
  }
  return worst;
}

// Distance table lookup. Counting every threshold that has been passed keeps
// the table order-independent, so a caller can raise one entry without
// reshuffling the others.
int levelIndexForDistance(float distance, const TerrainLodSettings &settings) {
  const int usable = terrainLodUsableLevels(settings);
  int index = 0;
  for (const float threshold : settings.distanceThresholds)
    if (std::isfinite(threshold) && distance >= threshold)
      ++index;
  return std::min(index, usable);
}

// Walks from the distance-selected candidate down toward Full and stops at the
// first level whose error against the full-resolution surface stays inside the
// tolerance. Error grows with the stride, so the walk is monotone: it can only
// pick a FINER level than the distance asked for, never a coarser one, and it
// always terminates at Full.
int geometricLevelIndex(const HeightField &field, int firstCellX, int firstCellZ,
                        int cellsX, int cellsZ, int candidate,
                        const TerrainLodSettings &settings) {
  int index = std::max(0, candidate);
  while (index > 0 && deviationIn(field, stepForIndex(index), firstCellX, firstCellZ,
                                  cellsX, cellsZ) > settings.geometricError)
    --index;
  return index;
}

bool chunkFits(const HeightField &field, const TerrainChunk &chunk) {
  return chunk.cellsX > 0 && chunk.cellsZ > 0 && chunk.firstCellX >= 0 &&
         chunk.firstCellZ >= 0 && chunk.firstCellX + chunk.cellsX <= field.cellsX &&
         chunk.firstCellZ + chunk.cellsZ <= field.cellsZ;
}

// The camera's distance to the NEAREST point of a chunk's footprint, which is
// not the same as the distance to its centre for a chunk wider than it is deep.
float nearestChunkDistance(const HeightField &field, const TerrainChunk &chunk,
                           Vec2 camera) {
  const Vec2 first = field.position(chunk.firstCellX, chunk.firstCellZ);
  const Vec2 last = field.position(chunk.firstCellX + chunk.cellsX,
                                   chunk.firstCellZ + chunk.cellsZ);
  const float dx = std::max({0.F, first.x - camera.x, camera.x - last.x});
  const float dz = std::max({0.F, first.y - camera.y, camera.y - last.y});
  return std::hypot(dx, dz);
}

} // namespace

std::string_view terrainLodName(TerrainLod level) {
  switch (level) {
  case TerrainLod::Full:
    return "full";
  case TerrainLod::Half:
    return "half";
  case TerrainLod::Quarter:
    return "quarter";
  case TerrainLod::Coarse:
    return "coarse";
  case TerrainLod::Off:
    break;
  }
  return "off";
}

int terrainLodUsableLevels(const TerrainLodSettings &settings) {
  // `maximumLevel` is a level count, so 4 permits every level including Off.
  const int byThresholds = int(settings.distanceThresholds.size());
  return std::clamp(settings.maximumLevel, 0,
                    std::min(terrainLodLevelCount - 1, byThresholds));
}

TerrainLod selectTerrainLod(const HeightField &field, Vec2 cameraPosition,
                            const TerrainLodSettings &settings) {
  // The field's own extent counts as a chunk even when the generator has not
  // partitioned it, so the answer never depends on chunk bookkeeping being
  // present.
  TerrainChunk extent{0, 0, field.cellsX, field.cellsZ};
  const TerrainChunk *nearest = &extent;
  float distance = nearestChunkDistance(field, extent, cameraPosition);
  for (const auto &chunk : field.chunks)
    if (chunkFits(field, chunk)) {
      const float candidate = nearestChunkDistance(field, chunk, cameraPosition);
      // On a tie a real chunk wins. The whole-extent fallback is the same
      // distance as its own edge chunks, and measuring it would put the
      // geometric walk back on the entire field.
      if (candidate <= distance) {
        distance = candidate;
        nearest = &chunk;
      }
    }
  if (!std::isfinite(distance))
    return TerrainLod::Full;

  const int selected = levelIndexForDistance(distance, settings);
  // Off has no reduced mesh whose error could be measured, so distance alone
  // decides it; a flat field is still not drawn past the last threshold.
  if (selected >= terrainLodIndex(TerrainLod::Off))
    return TerrainLod::Off;
  // The geometric-error term is measured on the nearest chunk only. A steep
  // chunk on the far side of the field must not dictate the detail of the ground
  // the camera is standing on, and this keeps the whole function O(cells in one
  // chunk) instead of O(cells in the field).
  return TerrainLod(geometricLevelIndex(field, nearest->firstCellX,
                                        nearest->firstCellZ, nearest->cellsX,
                                        nearest->cellsZ, selected, settings));
}

TerrainLod terrainLodForChunk(const HeightField &field, const TerrainChunk &chunk,
                              float maxDistance, const TerrainLodSettings &settings) {
  if (!chunkFits(field, chunk))
    return TerrainLod::Off;
  const float distance = std::isfinite(maxDistance) ? std::max(0.F, maxDistance) : 0.F;
  const int selected = levelIndexForDistance(distance, settings);
  if (selected >= terrainLodIndex(TerrainLod::Off))
    return TerrainLod::Off;
  return TerrainLod(geometricLevelIndex(field, chunk.firstCellX, chunk.firstCellZ,
                                        chunk.cellsX, chunk.cellsZ, selected,
                                        settings));
}

std::optional<TerrainLodMesh>
buildTerrainLodMesh(const HeightField &field, const TerrainChunk &chunk,
                     TerrainLod level, float skirtDepth) {
  if (level == TerrainLod::Off || !chunkFits(field, chunk))
    return std::nullopt;
  // The sampler reads the field through HeightField::index, which throws rather
  // than wrapping; a field whose buffers do not cover its own grid is refused
  // here instead of inside the loop.
  if (field.heights.size() < field.index(field.cellsX, field.cellsZ) + 1)
    return std::nullopt;

  const int step = terrainLodStep(level);
  const auto latticeX = makeLattice(chunk.cellsX, step);
  const auto latticeZ = makeLattice(chunk.cellsZ, step);
  const auto columns = latticeX.size();
  const auto rows = latticeZ.size();

  TerrainLodMesh mesh;
  mesh.sourceCells = std::size_t(step) * std::size_t(step);
  mesh.vertices.reserve(columns * rows);
  for (std::size_t row = 0; row < rows; ++row) {
    const int cellZ = chunk.firstCellZ + latticeZ.sample(row);
    for (std::size_t column = 0; column < columns; ++column) {
      const int cellX = chunk.firstCellX + latticeX.sample(column);
      // Read straight from the field rather than interpolating, so a vertex that
      // coincides with a full-resolution sample carries that sample's position
      // and height exactly.
      const Vec2 position = field.position(cellX, cellZ);
      mesh.vertices.push_back({position.x, field.height(cellX, cellZ), position.y});
    }
  }

  // Normals are recomputed from the reduced geometry. Sampling the field's own
  // normals would describe a surface that is no longer there, and the lighting
  // would disagree with the silhouette.
  mesh.normals.reserve(mesh.vertices.size());
  for (std::size_t row = 0; row < rows; ++row) {
    const std::size_t down = row == 0 ? 0 : row - 1;
    const std::size_t up = row + 1 < rows ? row + 1 : row;
    for (std::size_t column = 0; column < columns; ++column) {
      const std::size_t left = column == 0 ? 0 : column - 1;
      const std::size_t right = column + 1 < columns ? column + 1 : column;
      const Vec3 a = mesh.vertices.at(row * columns + left);
      const Vec3 b = mesh.vertices.at(row * columns + right);
      const Vec3 c = mesh.vertices.at(down * columns + column);
      const Vec3 d = mesh.vertices.at(up * columns + column);
      const Vec3 tangentX{b.x - a.x, b.y - a.y, b.z - a.z};
      const Vec3 tangentZ{d.x - c.x, d.y - c.y, d.z - c.z};
      // cross(tangentZ, tangentX) is the same convention the generator uses, so
      // a Full-level mesh reproduces the field's normals.
      const double nx = double(tangentZ.y) * tangentX.z - double(tangentZ.z) * tangentX.y;
      const double ny = double(tangentZ.z) * tangentX.x - double(tangentX.z) * tangentZ.x;
      const double nz = double(tangentZ.x) * tangentX.y - double(tangentZ.y) * tangentX.x;
      const double length = std::hypot(nx, ny, nz);
      if (length > 0)
        mesh.normals.push_back(
            {float(nx / length), float(ny / length), float(nz / length)});
      else
        mesh.normals.push_back({0.F, 1.F, 0.F});
    }
  }

  mesh.indices.reserve((columns > 0 && rows > 0 ? (columns - 1) * (rows - 1) : 0) * 6);
  for (std::size_t row = 0; row + 1 < rows; ++row)
    for (std::size_t column = 0; column + 1 < columns; ++column) {
      const std::size_t a = row * columns + column;
      const std::size_t b = (row + 1) * columns + column;
      const std::size_t c = row * columns + column + 1;
      const std::size_t d = (row + 1) * columns + column + 1;
      // Same order as the full-resolution chunk builder's two triangles.
      mesh.indices.insert(mesh.indices.end(), {a, b, c});
      mesh.indices.insert(mesh.indices.end(), {c, b, d});
    }

  // A Full-level boundary already IS the full-resolution edge, so it cannot crack
  // against a neighbour, and a caller that wants no skirt passes zero depth.
  if (level == TerrainLod::Full || !(skirtDepth > 0.F))
    return mesh;
  // A lattice with no interior has no boundary to fill.
  if (columns < 2 || rows < 2)
    return mesh;

  // Deepest full-resolution height along the chunk's shared edges. A neighbour
  // renders those same samples at its own level, so a skirt that stops above
  // this depth can still leave sky showing through the crack.
  float edgeMinimum = std::numeric_limits<float>::max();
  for (int x = 0; x <= chunk.cellsX; ++x)
    edgeMinimum = std::min({edgeMinimum,
                            field.height(chunk.firstCellX + x, chunk.firstCellZ),
                            field.height(chunk.firstCellX + x,
                                         chunk.firstCellZ + chunk.cellsZ)});
  for (int z = 0; z <= chunk.cellsZ; ++z)
    edgeMinimum = std::min({edgeMinimum,
                            field.height(chunk.firstCellX, chunk.firstCellZ + z),
                            field.height(chunk.firstCellX + chunk.cellsX,
                                         chunk.firstCellZ + z)});

  // The boundary ring, once each, in the order a clockwise walk from above
  // visits it. Corners belong to both of their edges but must not be emitted
  // twice, so each edge contributes everything except its far corner.
  std::vector<std::pair<std::size_t, std::size_t>> ring;
  ring.reserve(columns * 2 + rows * 2);
  for (std::size_t column = 0; column + 1 < columns; ++column)
    ring.emplace_back(column, 0);
  for (std::size_t row = 0; row + 1 < rows; ++row)
    ring.emplace_back(columns - 1, row);
  for (std::size_t column = columns - 1; column > 0; --column)
    ring.emplace_back(column, rows - 1);
  for (std::size_t row = rows - 1; row > 0; --row)
    ring.emplace_back(0, row);

  std::vector<float> depths;
  depths.reserve(ring.size());
  for (const auto &[column, row] : ring) {
    const Vec3 top = mesh.vertices.at(row * columns + column);
    const Vec3 normal = mesh.normals.at(row * columns + column);
    // Reach at least as far as the deepest full-resolution edge sample below
    // this vertex, and a little past it so the skirt is never exactly coplanar
    // with the surface it is hiding.
    const float reach = std::max(skirtDepth, top.y - edgeMinimum);
    const float depth = reach * 1.01F + 1e-3F;
    depths.push_back(depth);
    mesh.vertices.push_back({top.x, top.y - depth, top.z});
    mesh.normals.push_back(normal);
  }

  const std::size_t base = mesh.vertices.size() - ring.size();
  for (std::size_t slot = 0; slot < ring.size(); ++slot) {
    const std::size_t next = (slot + 1) % ring.size();
    const std::size_t topA = ring[slot].second * columns + ring[slot].first;
    const std::size_t topB = ring[next].second * columns + ring[next].first;
    // Wound so the skirt faces out of the chunk on every edge of the ring.
    mesh.indices.insert(mesh.indices.end(), {topA, base + next, base + slot});
    mesh.indices.insert(mesh.indices.end(), {topA, topB, base + next});
  }
  mesh.skirtDepths = std::move(depths);
  return mesh;
}
} // namespace demi::runtime