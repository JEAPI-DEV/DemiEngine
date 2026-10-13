#include "demi/runtime/terrain/TerrainLod.h"
#include "demi/runtime/terrain/TerrainLodGeometry.h"

#include "demi/runtime/terrain/TerrainGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace demi::runtime {
namespace {

// Distance table lookup. Counting every threshold that has been passed keeps
// the table order-independent, so a caller can raise one entry without
// reshuffling the others.
int levelIndexForDistance(float distance, const TerrainLodSettings &settings) {
  const int usable = terrainLodUsableLevels(settings);
  int index = 0;
  for (const float threshold : settings.distanceThresholds) {
    if (std::isfinite(threshold) && distance >= threshold) {
      ++index;
    }
  }
  return std::min(index, usable);
}

// Walks from the distance-selected candidate down toward Full and stops at the
// first level whose error against the full-resolution surface stays inside the
// tolerance. It only considers finer levels than the distance requested and
// always terminates at Full.
int geometricLevelIndex(const HeightField &field, int firstCellX,
                        int firstCellZ, int cellsX, int cellsZ, int candidate,
                        const TerrainLodSettings &settings) {
  int index = std::max(0, candidate);
  const TerrainChunk chunk{firstCellX, firstCellZ, cellsX, cellsZ};
  while (index > 0 &&
         terrainLodGeometricError(field, chunk, TerrainLod(index)) >
             settings.geometricError) {
    --index;
  }
  return index;
}

bool chunkFits(const HeightField &field, const TerrainChunk &chunk) {
  return chunk.cellsX > 0 && chunk.cellsZ > 0 && chunk.firstCellX >= 0 &&
         chunk.firstCellZ >= 0 && chunk.firstCellX <= field.cellsX &&
         chunk.cellsX <= field.cellsX - chunk.firstCellX &&
         chunk.firstCellZ <= field.cellsZ &&
         chunk.cellsZ <= field.cellsZ - chunk.firstCellZ;
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
  // The highest selectable ordinal is Off; zero keeps Full detail.
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
  for (const auto &chunk : field.chunks) {
    if (chunkFits(field, chunk)) {
      const float candidate =
          nearestChunkDistance(field, chunk, cameraPosition);
      // On a tie a real chunk wins. The whole-extent fallback is the same
      // distance as its own edge chunks, and measuring it would put the
      // geometric walk back on the entire field.
      if (candidate <= distance) {
        distance = candidate;
        nearest = &chunk;
      }
    }
  }
  if (!std::isfinite(distance)) {
    return TerrainLod::Full;
  }

  const int selected = levelIndexForDistance(distance, settings);
  // Off has no reduced mesh whose error could be measured, so distance alone
  // decides it; a flat field is still not drawn past the last threshold.
  if (selected >= terrainLodIndex(TerrainLod::Off)) {
    return TerrainLod::Off;
  }
  // The geometric-error term is measured on the nearest chunk only. A steep
  // chunk on the far side of the field must not dictate the detail of the
  // ground the camera is standing on, and this keeps the whole function O(cells
  // in one chunk) instead of O(cells in the field).
  return TerrainLod(geometricLevelIndex(field, nearest->firstCellX,
                                        nearest->firstCellZ, nearest->cellsX,
                                        nearest->cellsZ, selected, settings));
}

TerrainLod terrainLodForChunk(const HeightField &field,
                              const TerrainChunk &chunk, float maxDistance,
                              const TerrainLodSettings &settings) {
  if (!chunkFits(field, chunk)) {
    return TerrainLod::Off;
  }
  const float distance =
      std::isfinite(maxDistance) ? std::max(0.F, maxDistance) : 0.F;
  const int selected = levelIndexForDistance(distance, settings);
  if (selected >= terrainLodIndex(TerrainLod::Off)) {
    return TerrainLod::Off;
  }
  return TerrainLod(geometricLevelIndex(field, chunk.firstCellX,
                                        chunk.firstCellZ, chunk.cellsX,
                                        chunk.cellsZ, selected, settings));
}

std::optional<TerrainLodMesh> buildTerrainLodMesh(const HeightField &field,
                                                  const TerrainChunk &chunk,
                                                  TerrainLod level,
                                                  float skirtDepth) {
  if (level == TerrainLod::Off || !chunkFits(field, chunk)) {
    return std::nullopt;
  }
  // The sampler reads the field through HeightField::index, which throws rather
  // than wrapping; a field whose buffers do not cover its own grid is refused
  // here instead of inside the loop.
  if (field.heights.size() < field.index(field.cellsX, field.cellsZ) + 1) {
    return std::nullopt;
  }

  const int step = terrainLodStep(level);
  if (step <= 0) {
    return std::nullopt;
  }
  const auto topology = terrain_detail::buildTerrainLodTopology(chunk, level);
  const auto columns = topology.columns;
  const auto rows = topology.rows;
  TerrainLodMesh mesh;
  mesh.sourceCells = std::size_t(step) * std::size_t(step);
  mesh.vertices.reserve(topology.samples.size());
  mesh.sampleIndices.reserve(topology.samples.size());
  for (const auto sample : topology.samples) {
    const Vec2 position = field.position(sample.cellX, sample.cellZ);
    mesh.vertices.push_back(
        {position.x, field.height(sample.cellX, sample.cellZ), position.y});
    mesh.sampleIndices.push_back(field.index(sample.cellX, sample.cellZ));
  }
  mesh.indices = topology.indices;
  mesh.triangles = topology.triangles;

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
      const double nx =
          double(tangentZ.y) * tangentX.z - double(tangentZ.z) * tangentX.y;
      const double ny =
          double(tangentZ.z) * tangentX.x - double(tangentX.z) * tangentZ.x;
      const double nz =
          double(tangentZ.x) * tangentX.y - double(tangentZ.y) * tangentX.x;
      const double length = std::hypot(nx, ny, nz);
      if (length > 0) {
        mesh.normals.push_back(
            {float(nx / length), float(ny / length), float(nz / length)});
      } else {
        mesh.normals.push_back({0.F, 1.F, 0.F});
      }
    }
  }

  // Field normals define a single shared lighting boundary. They are also the
  // canonical Full normals, so Full publication remains visually identical.
  const auto sharedNormal = [&](const terrain_detail::TerrainLodSample sample) {
    const auto sampleIndex = field.index(sample.cellX, sample.cellZ);
    if (sampleIndex < field.normals.size()) {
      return field.normals.at(sampleIndex);
    }
    const int left = std::max(0, sample.cellX - 1);
    const int right = std::min(field.cellsX, sample.cellX + 1);
    const int down = std::max(0, sample.cellZ - 1);
    const int up = std::min(field.cellsZ, sample.cellZ + 1);
    const float dx = field.position(right, sample.cellZ).x -
                     field.position(left, sample.cellZ).x;
    const float dz = field.position(sample.cellX, up).y -
                     field.position(sample.cellX, down).y;
    const Vec3 normal{
        -(field.height(right, sample.cellZ) -
          field.height(left, sample.cellZ)) *
            dz,
        dx * dz,
        -(field.height(sample.cellX, up) - field.height(sample.cellX, down)) *
            dx};
    const float length = std::hypot(normal.x, normal.y, normal.z);
    return length > 0.0F
               ? Vec3{normal.x / length, normal.y / length, normal.z / length}
               : Vec3{0, 1, 0};
  };
  mesh.normals.resize(mesh.vertices.size());
  for (std::size_t vertex = 0; vertex < topology.samples.size(); ++vertex) {
    const auto sample = topology.samples[vertex];
    const bool boundary = sample.cellX == chunk.firstCellX ||
                          sample.cellX == chunk.firstCellX + chunk.cellsX ||
                          sample.cellZ == chunk.firstCellZ ||
                          sample.cellZ == chunk.firstCellZ + chunk.cellsZ;
    if (level == TerrainLod::Full || boundary) {
      mesh.normals[vertex] = sharedNormal(sample);
    }
  }

  if (level == TerrainLod::Full || !(skirtDepth > 0.0F)) {
    return mesh;
  }
  float edgeMinimum = std::numeric_limits<float>::max();
  for (const auto vertex : topology.boundary) {
    edgeMinimum = std::min(edgeMinimum, mesh.vertices[vertex].y);
  }
  const auto skirtBase = mesh.vertices.size();
  for (const auto vertex : topology.boundary) {
    const Vec3 top = mesh.vertices[vertex];
    const float reach = std::max(skirtDepth, top.y - edgeMinimum);
    const float depth = reach * 1.01F + 1e-3F;
    mesh.skirtDepths.push_back(depth);
    mesh.vertices.push_back({top.x, top.y - depth, top.z});
    mesh.normals.push_back(mesh.normals[vertex]);
    mesh.sampleIndices.push_back(mesh.sampleIndices[vertex]);
  }
  for (std::size_t segment = 0; segment < topology.boundary.size(); ++segment) {
    const auto next = (segment + 1) % topology.boundary.size();
    const auto firstTop = topology.boundary[segment];
    const auto lastTop = topology.boundary[next];
    const auto first = topology.samples[firstTop];
    const auto last = topology.samples[lastTop];
    TerrainLodTriangle source{.firstCellX = std::min(first.cellX, last.cellX),
                              .firstCellZ = std::min(first.cellZ, last.cellZ),
                              .cellsX = 1,
                              .cellsZ = 1,
                              .skirt = true};
    if (source.firstCellX == chunk.firstCellX + chunk.cellsX) {
      --source.firstCellX;
    }
    if (source.firstCellZ == chunk.firstCellZ + chunk.cellsZ) {
      --source.firstCellZ;
    }
    mesh.indices.insert(mesh.indices.end(),
                        {firstTop, skirtBase + next, skirtBase + segment,
                         firstTop, lastTop, skirtBase + next});
    mesh.triangles.push_back(source);
    mesh.triangles.push_back(source);
  }
  return mesh;
}
} // namespace demi::runtime
