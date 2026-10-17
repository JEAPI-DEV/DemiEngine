#include "demi/runtime/terrain/TerrainLodGeometry.h"

#include "demi/runtime/terrain/TerrainHeightField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace demi::runtime {
namespace {

using terrain_detail::TerrainLodSample;
using terrain_detail::TerrainLodTopology;

std::vector<int> latticeSamples(int count, int step) {
  std::vector<int> samples;
  for (int offset = 0; offset < count; offset += step) {
    samples.push_back(offset);
  }
  samples.push_back(count);
  return samples;
}

class TopologyBuilder {
public:
  explicit TopologyBuilder(const TerrainChunk &chunk) : chunk_(chunk) {}

  TerrainLodTopology build(TerrainLod level) {
    const auto columns = latticeSamples(chunk_.cellsX, terrainLodStep(level));
    const auto rows = latticeSamples(chunk_.cellsZ, terrainLodStep(level));
    topology_.columns = columns.size();
    topology_.rows = rows.size();
    // Preserve regular lattice order for consumers inspecting interior samples.
    for (const int z : rows) {
      for (const int x : columns) {
        vertex(x, z);
      }
    }
    for (std::size_t row = 0; row + 1 < rows.size(); ++row) {
      for (std::size_t column = 0; column + 1 < columns.size(); ++column) {
        const auto a = vertex(columns[column], rows[row]);
        const auto b = vertex(columns[column], rows[row + 1]);
        const auto c = vertex(columns[column + 1], rows[row]);
        const auto d = vertex(columns[column + 1], rows[row + 1]);
        const TerrainLodTriangle source{
            .firstCellX = chunk_.firstCellX + columns[column],
            .firstCellZ = chunk_.firstCellZ + rows[row],
            .cellsX = columns[column + 1] - columns[column],
            .cellsZ = rows[row + 1] - rows[row]};
        const auto firstTriangle = topology_.triangles.size();
        triangle({a, b, c}, source);
        triangle({c, b, d}, source);
        topology_.triangles[firstTriangle].sourceCells =
            std::size_t(source.cellsX) * std::size_t(source.cellsZ);
      }
    }
    for (int x = 0; x < chunk_.cellsX; ++x) {
      topology_.boundary.push_back(vertex(x, 0));
    }
    for (int z = 0; z < chunk_.cellsZ; ++z) {
      topology_.boundary.push_back(vertex(chunk_.cellsX, z));
    }
    for (int x = chunk_.cellsX; x > 0; --x) {
      topology_.boundary.push_back(vertex(x, chunk_.cellsZ));
    }
    for (int z = chunk_.cellsZ; z > 0; --z) {
      topology_.boundary.push_back(vertex(0, z));
    }
    return std::move(topology_);
  }

private:
  std::size_t vertex(int x, int z) {
    const auto key =
        (std::uint64_t(std::uint32_t(x)) << 32U) | std::uint32_t(z);
    const auto found = vertices_.find(key);
    if (found != vertices_.end()) {
      return found->second;
    }
    const auto index = topology_.samples.size();
    vertices_.emplace(key, index);
    topology_.samples.push_back({chunk_.firstCellX + x, chunk_.firstCellZ + z});
    return index;
  }

  void triangle(const std::array<std::size_t, 3> indices,
                const TerrainLodTriangle &source) {
    for (std::size_t edge = 0; edge < 3; ++edge) {
      const auto first = topology_.samples[indices[edge]];
      const auto last = topology_.samples[indices[(edge + 1) % 3]];
      const bool verticalBoundary =
          first.cellX == last.cellX &&
          (first.cellX == chunk_.firstCellX ||
           first.cellX == chunk_.firstCellX + chunk_.cellsX);
      const bool horizontalBoundary =
          first.cellZ == last.cellZ &&
          (first.cellZ == chunk_.firstCellZ ||
           first.cellZ == chunk_.firstCellZ + chunk_.cellsZ);
      const int length = std::max(std::abs(last.cellX - first.cellX),
                                  std::abs(last.cellZ - first.cellZ));
      if ((!verticalBoundary && !horizontalBoundary) || length <= 1) {
        continue;
      }
      const int dx = (last.cellX - first.cellX) / length;
      const int dz = (last.cellZ - first.cellZ) / length;
      auto previous = indices[edge];
      const auto opposite = indices[(edge + 2) % 3];
      for (int offset = 1; offset <= length; ++offset) {
        const auto next = vertex(first.cellX - chunk_.firstCellX + offset * dx,
                                 first.cellZ - chunk_.firstCellZ + offset * dz);
        // A corner triangle can have two perimeter edges. Recursion refines the
        // remaining one while retaining winding and all nonzero-area triangles.
        triangle({previous, next, opposite}, source);
        previous = next;
      }
      return;
    }
    topology_.indices.insert(topology_.indices.end(), indices.begin(),
                             indices.end());
    topology_.triangles.push_back(source);
  }

  const TerrainChunk &chunk_;
  TerrainLodTopology topology_;
  std::unordered_map<std::uint64_t, std::size_t> vertices_;
};

struct Point {
  double x = 0;
  double z = 0;
};

struct HeightTriangle {
  std::array<Point, 3> points;
  std::array<double, 3> heights;
};

double cross(Point first, Point second, Point third) {
  return (second.x - first.x) * (third.z - first.z) -
         (second.z - first.z) * (third.x - first.x);
}

HeightTriangle heightTriangle(const HeightField &field,
                              const std::array<TerrainLodSample, 3> &samples) {
  HeightTriangle triangle;
  for (std::size_t index = 0; index < 3; ++index) {
    triangle.points[index] = {double(samples[index].cellX),
                              double(samples[index].cellZ)};
    triangle.heights[index] =
        field.height(samples[index].cellX, samples[index].cellZ);
  }
  return triangle;
}

double heightAt(const HeightTriangle &triangle, Point point) {
  const double area =
      cross(triangle.points[0], triangle.points[1], triangle.points[2]);
  const double first =
      cross(triangle.points[1], triangle.points[2], point) / area;
  const double second =
      cross(triangle.points[2], triangle.points[0], point) / area;
  return first * triangle.heights[0] + second * triangle.heights[1] +
         (1.0 - first - second) * triangle.heights[2];
}

double overlayError(const HeightTriangle &full, const HeightTriangle &reduced) {
  // Clipping a triangle by three half-planes has at most six vertices. Two
  // fixed buffers avoid allocation for every Full/reduced triangle pair.
  std::array<Point, 8> polygon{};
  std::copy(full.points.begin(), full.points.end(), polygon.begin());
  std::size_t polygonSize = full.points.size();
  const double orientation =
      cross(reduced.points[0], reduced.points[1], reduced.points[2]) < 0.0
          ? -1.0
          : 1.0;
  for (std::size_t edge = 0; edge < 3 && polygonSize > 0; ++edge) {
    const Point first = reduced.points[edge];
    const Point last = reduced.points[(edge + 1) % 3];
    std::array<Point, 8> clipped{};
    std::size_t clippedSize = 0;
    Point previous = polygon[polygonSize - 1];
    double previousSide = orientation * cross(first, last, previous);
    for (std::size_t vertex = 0; vertex < polygonSize; ++vertex) {
      const Point point = polygon[vertex];
      const double side = orientation * cross(first, last, point);
      const bool wasInside = previousSide >= 0.0;
      const bool isInside = side >= 0.0;
      if (wasInside != isInside) {
        const double fraction = previousSide / (previousSide - side);
        clipped[clippedSize++] = {
            previous.x + fraction * (point.x - previous.x),
            previous.z + fraction * (point.z - previous.z)};
      }
      if (isInside) {
        clipped[clippedSize++] = point;
      }
      previous = point;
      previousSide = side;
    }
    polygon = clipped;
    polygonSize = clippedSize;
  }
  double worst = 0;
  // The difference between two triangle height planes is affine. Its absolute
  // maximum on their convex intersection occurs at a polygon vertex, including
  // intersections between Full and reduced diagonals that are not grid samples.
  for (std::size_t vertex = 0; vertex < polygonSize; ++vertex) {
    const Point point = polygon[vertex];
    worst = std::max(
        worst, std::abs(heightAt(full, point) - heightAt(reduced, point)));
  }
  return worst;
}

} // namespace

terrain_detail::TerrainLodTopology
terrain_detail::buildTerrainLodTopology(const TerrainChunk &chunk,
                                        TerrainLod level) {
  if (chunk.cellsX <= 0 || chunk.cellsZ <= 0 || terrainLodStep(level) <= 0) {
    return {};
  }
  return TopologyBuilder(chunk).build(level);
}

std::size_t terrainLodSurfaceTriangleCount(int cellsX, int cellsZ,
                                           TerrainLod level) {
  const int step = terrainLodStep(level);
  if (cellsX <= 0 || cellsZ <= 0 || step <= 0) {
    return 0;
  }
  const auto columns = std::size_t(cellsX / step + (cellsX % step != 0));
  const auto rows = std::size_t(cellsZ / step + (cellsZ % step != 0));
  return 2 * columns * rows + 2 * (std::size_t(cellsX) - columns) +
         2 * (std::size_t(cellsZ) - rows);
}

float terrainLodGeometricError(const HeightField &field,
                               const TerrainChunk &chunk, TerrainLod level) {
  if (field.cellsX < 0 || field.cellsZ < 0 || chunk.cellsX <= 0 ||
      chunk.cellsZ <= 0 || chunk.firstCellX < 0 || chunk.firstCellZ < 0 ||
      chunk.firstCellX > field.cellsX - chunk.cellsX ||
      chunk.firstCellZ > field.cellsZ - chunk.cellsZ ||
      terrainLodStep(level) <= 0) {
    throw std::invalid_argument(
        "Terrain LOD error requires a valid chunk and surface level");
  }
  if (level == TerrainLod::Full) {
    return 0.0F;
  }
  // Every reduced topology interpolates the same horizontal plane when all
  // samples, including the perimeter, agree. Avoid constructing topology and
  // clipping triangle overlays for this exact and common case.
  const float firstHeight = field.height(chunk.firstCellX, chunk.firstCellZ);
  bool constantHeight = std::isfinite(firstHeight);
  for (int z = chunk.firstCellZ;
       constantHeight && z <= chunk.firstCellZ + chunk.cellsZ; ++z) {
    for (int x = chunk.firstCellX; x <= chunk.firstCellX + chunk.cellsX; ++x) {
      if (field.height(x, z) != firstHeight) {
        constantHeight = false;
        break;
      }
    }
  }
  if (constantHeight) {
    return 0.F;
  }
  const auto topology = terrain_detail::buildTerrainLodTopology(chunk, level);
  double worst = 0;
  for (std::size_t index = 0; index < topology.triangles.size(); ++index) {
    std::array<TerrainLodSample, 3> samples;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      samples[corner] = topology.samples[topology.indices[index * 3 + corner]];
    }
    const auto reduced = heightTriangle(field, samples);
    const auto minimumX =
        std::min({samples[0].cellX, samples[1].cellX, samples[2].cellX});
    const auto maximumX =
        std::max({samples[0].cellX, samples[1].cellX, samples[2].cellX});
    const auto minimumZ =
        std::min({samples[0].cellZ, samples[1].cellZ, samples[2].cellZ});
    const auto maximumZ =
        std::max({samples[0].cellZ, samples[1].cellZ, samples[2].cellZ});
    for (int z = minimumZ; z < maximumZ; ++z) {
      for (int x = minimumX; x < maximumX; ++x) {
        const auto first =
            heightTriangle(field, {{{x, z}, {x, z + 1}, {x + 1, z}}});
        const auto second =
            heightTriangle(field, {{{x + 1, z}, {x, z + 1}, {x + 1, z + 1}}});
        worst = std::max({worst, overlayError(first, reduced),
                          overlayError(second, reduced)});
      }
    }
  }
  const float rounded = static_cast<float>(worst);
  if (static_cast<double>(rounded) < worst) {
    return std::nextafter(rounded, std::numeric_limits<float>::infinity());
  }
  return rounded;
}

} // namespace demi::runtime
