#include "demi/runtime/terrain/TerrainGraphPathMask.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace demi::runtime::terrain_graph_detail {
namespace {

struct PathPoint {
  double x;
  double z;
};

double segmentDistanceSquared(PathPoint point, PathPoint start, PathPoint end) {
  const double dx = end.x - start.x;
  const double dz = end.z - start.z;
  const double lengthSquared = dx * dx + dz * dz;
  const double projection =
      lengthSquared == 0.0
          ? 0.0
          : std::clamp(((point.x - start.x) * dx + (point.z - start.z) * dz) /
                           lengthSquared,
                       0.0, 1.0);
  const double offsetX = point.x - (start.x + projection * dx);
  const double offsetZ = point.z - (start.z + projection * dz);
  return offsetX * offsetX + offsetZ * offsetZ;
}

} // namespace

std::optional<GraphNodeResult>
evaluateTerrainPathMask(const nlohmann::json &parameters, GraphField source,
                        std::stop_token stop) {
  const double width = parameters.at("width").get<double>();
  const double falloff = parameters.at("falloff").get<double>();
  const auto &authoredPoints = parameters.at("points");
  if (!source || !std::isfinite(width) || !std::isfinite(falloff) ||
      width < 0.0 || falloff < 0.0 || !authoredPoints.is_array() ||
      authoredPoints.size() < 2) {
    throw std::invalid_argument("Path mask requires a field, two points and "
                                "finite nonnegative width/falloff");
  }
  std::vector<PathPoint> points;
  points.reserve(authoredPoints.size());
  for (const auto &point : authoredPoints) {
    if (!point.is_array() || point.size() != 2) {
      throw std::invalid_argument("Path mask points must be [x,z] pairs");
    }
    PathPoint parsed{point.at(0).get<double>(), point.at(1).get<double>()};
    const double maximumCoordinate = std::numeric_limits<float>::max();
    if (!std::isfinite(parsed.x) || !std::isfinite(parsed.z) ||
        std::abs(parsed.x) > maximumCoordinate ||
        std::abs(parsed.z) > maximumCoordinate) {
      throw std::invalid_argument(
          "Path mask points must fit finite terrain coordinates");
    }
    points.push_back(parsed);
  }

  auto mask = std::make_shared<GraphMask>();
  mask->source = std::move(source);
  mask->samples.resize(mask->source->heights.size());
  for (int z = 0; z <= mask->source->cellsZ; ++z) {
    if (stop.stop_requested()) {
      return std::nullopt;
    }
    for (int x = 0; x <= mask->source->cellsX; ++x) {
      const auto position = mask->source->position(x, z);
      const PathPoint sample{position.x, position.y};
      double nearestSquared = std::numeric_limits<double>::infinity();
      for (std::size_t segment = 1; segment < points.size(); ++segment) {
        if ((segment & 255) == 0 && stop.stop_requested()) {
          return std::nullopt;
        }
        nearestSquared = std::min(
            nearestSquared, segmentDistanceSquared(sample, points[segment - 1],
                                                   points[segment]));
      }
      const double distance = std::sqrt(nearestSquared);
      double weight = distance <= width ? 1.0 : 0.0;
      if (distance > width && falloff > 0.0) {
        const double transition =
            std::clamp((distance - width) / falloff, 0.0, 1.0);
        weight = 1.0 - transition * transition * (3.0 - 2.0 * transition);
      }
      mask->samples.set(mask->source->index(x, z), static_cast<float>(weight));
    }
  }
  GraphNodeResult result;
  result.outputs["mask"] = std::shared_ptr<const GraphMask>(std::move(mask));
  return result;
}

} // namespace demi::runtime::terrain_graph_detail
