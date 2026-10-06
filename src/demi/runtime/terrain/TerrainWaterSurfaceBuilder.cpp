#include "demi/runtime/terrain/TerrainWaterSurfaceBuilder.h"

#include "demi/runtime/terrain/TerrainWaterGeometry.h"
#include <map>

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
  std::map<std::pair<float, float>, std::size_t> vertexAt;
  const auto vertex = [&](const WaterGeometryVertex &point) {
    const auto position = std::pair{float(point.x), float(point.z)};
    const auto [entry, inserted] =
        vertexAt.try_emplace(position, surface.vertices.size());
    if (!inserted)
      return entry->second;
    surface.vertices.push_back({position.first, spec.level, position.second});
    surface.normals.push_back({0.F, 1.F, 0.F});
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
        corners[i] = {position.x, position.y,
                      double(spec.level) - double(ground[sample]),
                      waterOwnershipMask(field.wet[sample], field.body[sample],
                                         bodyIndex)};
      }
      for (const auto &triangle : waterCellTriangles) {
        const bool hasWetCorner =
            std::ranges::any_of(triangle, [&](auto corner) {
              return corners[corner].coverage > 0 && corners[corner].depth > 0;
            });
        if (!hasWetCorner)
          continue;
        const auto polygon = clipWaterTriangle(
            {corners[triangle[0]], corners[triangle[1]], corners[triangle[2]]});
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
          surface.indices.insert(surface.indices.end(),
                                 {vertex(a), vertex(b), vertex(c)});
        }
      }
    }
  }
  return surface;
}
} // namespace demi::runtime::terrain_water_detail
