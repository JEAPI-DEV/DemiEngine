#include "demi/runtime/terrain/TerrainWaterSurfaceBuilder.h"

#include "demi/runtime/terrain/TerrainWaterGeometry.h"
#include <map>
#include <tuple>

namespace demi::runtime::terrain_water_detail {
TerrainWaterSurface buildTerrainWaterSurface(const TerrainWaterBodySpec &spec,
                                             std::size_t bodyIndex,
                                             const WaterLevelField &field,
                                             std::span<const float> ground,
                                             std::stop_token stop) {
  TerrainWaterSurface surface;
  surface.id = spec.id;
  surface.kind = spec.kind;
  surface.level = spec.level;
  surface.riverSurfaceMode = spec.riverSurfaceMode;
  using VertexKey = std::tuple<float, float, float, float, float>;
  std::map<VertexKey, std::size_t> vertexAt;
  const auto vertex = [&](const WaterGeometryVertex &point, Vec3 normal) {
    const auto position = std::pair{float(point.x), float(point.z)};
    const VertexKey key{position.first, position.second, normal.x, normal.y,
                        normal.z};
    const auto [entry, inserted] =
        vertexAt.try_emplace(key, surface.vertices.size());
    if (!inserted)
      return entry->second;
    surface.vertices.push_back(
        {position.first, float(point.level), position.second});
    surface.normals.push_back(normal);
    surface.depth.push_back(float(std::max(0.0, point.depth)));
    return entry->second;
  };
  for (int z = 0; z < field.cellsZ; ++z) {
    if (stop.stop_requested())
      return surface;
    for (int x = 0; x < field.cellsX; ++x) {
      if ((x & 255) == 0 && stop.stop_requested())
        return surface;
      std::array<WaterGeometryVertex, 4> corners;
      const std::array<std::pair<int, int>, 4> coordinates{
          std::pair{x, z}, std::pair{x + 1, z}, std::pair{x, z + 1},
          std::pair{x + 1, z + 1}};
      for (std::size_t i = 0; i < corners.size(); ++i) {
        const auto [cx, cz] = coordinates[i];
        const auto sample = field.sampleAt(cx, cz);
        const auto position = field.position(cx, cz);
        const auto level = field.bodyLevels.at(bodyIndex).at(sample);
        corners[i] = {position.x, position.y,
                      double(level) - double(ground[sample]),
                      waterOwnershipMask(field.wet[sample], field.body[sample],
                                         bodyIndex),
                      level};
      }
      for (const auto &triangle : waterCellTriangles) {
        const bool hasWetCorner =
            std::ranges::any_of(triangle, [&](auto corner) {
              return corners[corner].coverage > 0 && corners[corner].depth > 0;
            });
        if (!hasWetCorner)
          continue;
        const std::array<WaterGeometryVertex, 3> sourceTriangle{
            corners[triangle[0]], corners[triangle[1]], corners[triangle[2]]};
        const auto normal = waterTriangleNormal(sourceTriangle);
        const auto polygon = clipWaterTriangle(sourceTriangle);
        for (std::size_t i = 1; i + 1 < polygon.count; ++i) {
          const auto &a = polygon.vertices[0];
          const auto &b = polygon.vertices[i];
          const auto &c = polygon.vertices[i + 1];
          // Float conversion can collapse a tiny polygon, even if its area
          // was positive in the double-precision clipping calculation.
          const double area = (double(float(b.z)) - float(a.z)) *
                                  (double(float(c.x)) - float(a.x)) -
                              (double(float(b.x)) - float(a.x)) *
                                  (double(float(c.z)) - float(a.z));
          if (!(area > 0.0))
            continue;
          surface.indices.insert(
              surface.indices.end(),
              {vertex(a, normal), vertex(b, normal), vertex(c, normal)});
        }
      }
    }
  }
  return surface;
}
} // namespace demi::runtime::terrain_water_detail
