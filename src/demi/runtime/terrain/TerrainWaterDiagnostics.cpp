#include "demi/runtime/terrain/TerrainWaterDiagnostics.h"
#include "demi/runtime/terrain/TerrainHeightField.h"

#include <array>
#include <cmath>
#include <stdexcept>

namespace demi::runtime {
std::vector<std::string>
terrainWaterContainmentWarnings(const HeightField &ground,
                                const TerrainWaterAuthoring &authoring) {
  std::vector<std::string> warnings;
  if (ground.cellsX <= 0 || ground.cellsZ <= 0 ||
      !std::isfinite(ground.size.x) || !std::isfinite(ground.size.y) ||
      ground.size.x <= 0 || ground.size.y <= 0)
    throw std::invalid_argument(
        "Water containment requires a valid terrain grid");
  const auto coverage = terrain_water_detail::buildWaterLevelField(
      ground.size, ground.cellsX, ground.cellsZ, ground.heights, authoring);
  if (coverage.samples() != ground.heights.size())
    throw std::invalid_argument(
        "Water containment sample count does not match the terrain grid");
  std::vector<bool> spills(authoring.bodies.size());
  constexpr std::array<std::array<int, 2>, 4> neighbours{
      {{{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}}}};
  for (int z = 0; z <= ground.cellsZ; ++z) {
    for (int x = 0; x <= ground.cellsX; ++x) {
      const auto sample = coverage.sampleAt(x, z);
      if (!coverage.wet[sample])
        continue;
      const auto bodyIndex = coverage.body[sample];
      const auto &body = authoring.bodies.at(bodyIndex);
      if (body.kind != TerrainWaterBody::Lake || body.radius <= 0 ||
          spills[bodyIndex])
        continue;
      for (const auto &offset : neighbours) {
        const int nextX = x + offset[0], nextZ = z + offset[1];
        if (nextX < 0 || nextZ < 0 || nextX > ground.cellsX ||
            nextZ > ground.cellsZ) {
          spills[bodyIndex] = ground.heights[sample] < body.level;
          if (spills[bodyIndex])
            break;
          continue;
        }
        const auto next = coverage.sampleAt(nextX, nextZ);
        if (!coverage.wet[next] && ground.heights[next] < body.level) {
          spills[bodyIndex] = true;
          break;
        }
      }
    }
  }
  for (std::size_t body = 0; body < spills.size(); ++body)
    if (spills[body])
      warnings.push_back(
          "Lake '" + authoring.bodies[body].id +
          "' reaches its authored boundary below the water level. It is not "
          "contained by terrain banks; lower its level, enlarge its boundary, "
          "or sculpt banks. No flood simulation is performed.");
  return warnings;
}
} // namespace demi::runtime
