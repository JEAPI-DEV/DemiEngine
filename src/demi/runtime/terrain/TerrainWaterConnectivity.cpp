#include "demi/runtime/terrain/TerrainWaterConnectivity.h"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace demi::runtime::terrain_water_detail {
std::vector<std::uint8_t>
connectedLakeCoverage(Vec2 size, int cellsX, int cellsZ, Vec2 centre,
                      std::span<const std::uint8_t> wet, std::stop_token stop) {
  if (stop.stop_requested())
    return {};
  if (cellsX <= 0 || cellsZ <= 0 || !std::isfinite(size.x) ||
      !std::isfinite(size.y) || size.x <= 0 || size.y <= 0 ||
      !std::isfinite(centre.x) || !std::isfinite(centre.y))
    throw std::invalid_argument(
        "Lake connectivity requires a valid grid and centre");
  const auto columns = std::size_t(cellsX) + 1;
  const auto rows = std::size_t(cellsZ) + 1;
  if (columns > std::numeric_limits<std::size_t>::max() / rows ||
      wet.size() != columns * rows)
    throw std::invalid_argument(
        "Lake connectivity mask does not match the grid");
  std::vector<std::uint8_t> connected(wet.size());
  std::size_t seed = wet.size();
  double nearest = std::numeric_limits<double>::infinity();
  for (std::size_t sample = 0; sample < wet.size(); ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested())
      return {};
    if (!wet[sample])
      continue;
    const double dx = double(sample % columns) * size.x / cellsX - centre.x;
    const double dz = double(sample / columns) * size.y / cellsZ - centre.y;
    const double distance = dx * dx + dz * dz;
    if (distance < nearest) {
      nearest = distance;
      seed = sample;
    }
  }
  if (seed == wet.size())
    return connected;
  std::vector<std::size_t> pending{seed};
  connected[seed] = 1;
  constexpr std::array<std::array<int, 2>, 6> edges{
      {{{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}}, {{-1, 1}}, {{1, -1}}}};
  for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
    if ((cursor & 255) == 0 && stop.stop_requested())
      return {};
    const auto sample = pending[cursor];
    const int x = int(sample % columns), z = int(sample / columns);
    for (const auto &edge : edges) {
      const int nx = x + edge[0], nz = z + edge[1];
      if (nx < 0 || nz < 0 || nx > cellsX || nz > cellsZ)
        continue;
      const auto next = std::size_t(nz) * columns + std::size_t(nx);
      if (wet[next] && !connected[next]) {
        connected[next] = 1;
        pending.push_back(next);
      }
    }
  }
  return connected;
}
} // namespace demi::runtime::terrain_water_detail
