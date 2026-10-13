#include "demi/runtime/terrain/TerrainGraphSpatialMasks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace demi::runtime::terrain_graph_detail {
namespace {

struct SpatialPoint {
  double x;
  double z;
};

double segmentDistanceSquared(SpatialPoint point, SpatialPoint start,
                              SpatialPoint end) {
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

std::optional<std::vector<SpatialPoint>>
parsePoints(const nlohmann::json &authoredPoints, std::size_t minimum,
            std::stop_token stop) {
  if (!authoredPoints.is_array() || authoredPoints.size() < minimum) {
    throw std::invalid_argument("Spatial mask has too few points");
  }
  std::vector<SpatialPoint> points;
  points.reserve(authoredPoints.size());
  for (const auto &point : authoredPoints) {
    if (stop.stop_requested()) {
      return std::nullopt;
    }
    if (!point.is_array() || point.size() != 2) {
      throw std::invalid_argument("Spatial mask points must be [x,z] pairs");
    }
    if (!point.at(0).is_number() || !point.at(1).is_number()) {
      throw std::invalid_argument("Spatial mask coordinates must be numbers");
    }
    SpatialPoint parsed{point.at(0).get<double>(), point.at(1).get<double>()};
    const double maximumCoordinate = std::numeric_limits<float>::max();
    if (!std::isfinite(parsed.x) || !std::isfinite(parsed.z) ||
        std::abs(parsed.x) > maximumCoordinate ||
        std::abs(parsed.z) > maximumCoordinate) {
      throw std::invalid_argument(
          "Spatial mask points must fit finite terrain coordinates");
    }
    points.push_back(parsed);
  }
  return points;
}

bool hasNoncollinearPoints(const std::vector<SpatialPoint> &points,
                           std::stop_token stop) {
  const SpatialPoint origin = points.front();
  std::size_t second = 1;
  while (second < points.size() && points[second].x == origin.x &&
         points[second].z == origin.z) {
    if (stop.stop_requested()) {
      return false;
    }
    ++second;
  }
  if (second == points.size()) {
    return false;
  }
  const double dx = points[second].x - origin.x;
  const double dz = points[second].z - origin.z;
  for (const SpatialPoint point : points) {
    if (stop.stop_requested()) {
      return false;
    }
    const double cross = dx * (point.z - origin.z) - dz * (point.x - origin.x);
    if (cross != 0.0) {
      return true;
    }
  }
  return false;
}

std::optional<std::vector<SpatialPoint>>
validatedPoints(const nlohmann::json &authoredPoints, bool polygon,
                std::stop_token stop) {
  auto points = parsePoints(authoredPoints, polygon ? 3 : 2, stop);
  if (!points) {
    return std::nullopt;
  }
  if (polygon && !hasNoncollinearPoints(*points, stop)) {
    if (stop.stop_requested()) {
      return std::nullopt;
    }
    throw std::invalid_argument(
        "Polygon mask requires three noncollinear points");
  }
  return points;
}

double outsideWeight(double distance, double falloff) {
  if (distance <= 0.0) {
    return 1.0;
  }
  if (falloff == 0.0) {
    return 0.0;
  }
  const double transition = std::clamp(distance / falloff, 0.0, 1.0);
  return 1.0 - transition * transition * (3.0 - 2.0 * transition);
}

std::optional<GraphNodeResult>
evaluateSpatialMask(const nlohmann::json &parameters, GraphField source,
                    std::stop_token stop, bool polygon) {
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  const double width = polygon ? 0.0 : parameters.at("width").get<double>();
  const double falloff = parameters.at("falloff").get<double>();
  if (!source || !std::isfinite(width) || !std::isfinite(falloff) ||
      width < 0.0 || falloff < 0.0) {
    throw std::invalid_argument(
        "Spatial mask requires a field and finite nonnegative width/falloff");
  }
  auto parsed = validatedPoints(parameters.at("points"), polygon, stop);
  if (!parsed) {
    return std::nullopt;
  }
  const auto &points = *parsed;
  auto mask = std::make_shared<GraphMask>();
  mask->source = std::move(source);
  mask->samples.resize(mask->source->heights.size());
  const std::size_t segmentCount = polygon ? points.size() : points.size() - 1;
  for (int z = 0; z <= mask->source->cellsZ; ++z) {
    for (int x = 0; x <= mask->source->cellsX; ++x) {
      if (stop.stop_requested()) {
        return std::nullopt;
      }
      const auto position = mask->source->position(x, z);
      const SpatialPoint sample{position.x, position.y};
      double nearestSquared = std::numeric_limits<double>::infinity();
      bool inside = false;
      bool onBoundary = false;
      for (std::size_t segment = 0; segment < segmentCount; ++segment) {
        if ((segment & 255) == 0 && stop.stop_requested()) {
          return std::nullopt;
        }
        const SpatialPoint start = points[segment];
        const SpatialPoint end = points[(segment + 1) % points.size()];
        nearestSquared = std::min(nearestSquared,
                                  segmentDistanceSquared(sample, start, end));
        if (polygon) {
          const double edgeCross = (end.x - start.x) * (sample.z - start.z) -
                                   (end.z - start.z) * (sample.x - start.x);
          const bool withinEdgeBounds = sample.x >= std::min(start.x, end.x) &&
                                        sample.x <= std::max(start.x, end.x) &&
                                        sample.z >= std::min(start.z, end.z) &&
                                        sample.z <= std::max(start.z, end.z);
          if (edgeCross == 0.0 && withinEdgeBounds) {
            onBoundary = true;
          }
          // Half-open crossings count a shared vertex once. Horizontal and
          // repeated edges do not cross; boundary membership is handled
          // explicitly.
          if ((start.z > sample.z) != (end.z > sample.z)) {
            const double crossingX = start.x + (sample.z - start.z) *
                                                   (end.x - start.x) /
                                                   (end.z - start.z);
            if (sample.x < crossingX) {
              inside = !inside;
            }
          }
        }
      }
      const double distance = std::sqrt(nearestSquared);
      const double weight = (inside || onBoundary)
                                ? 1.0
                                : outsideWeight(distance - width, falloff);
      mask->samples.set(mask->source->index(x, z), static_cast<float>(weight));
    }
  }
  GraphNodeResult result;
  result.outputs["mask"] = std::shared_ptr<const GraphMask>(std::move(mask));
  return result;
}

} // namespace

void validateTerrainSpatialMaskPoints(const nlohmann::json &points,
                                      bool polygon) {
  (void)validatedPoints(points, polygon, {});
}

std::optional<GraphNodeResult>
evaluateTerrainPathMask(const nlohmann::json &parameters, GraphField source,
                        std::stop_token stop) {
  return evaluateSpatialMask(parameters, std::move(source), stop, false);
}

std::optional<GraphNodeResult>
evaluateTerrainPolygonMask(const nlohmann::json &parameters, GraphField source,
                           std::stop_token stop) {
  return evaluateSpatialMask(parameters, std::move(source), stop, true);
}

} // namespace demi::runtime::terrain_graph_detail
