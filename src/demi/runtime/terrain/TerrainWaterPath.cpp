#include "demi/runtime/terrain/TerrainWaterPath.h"

#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace demi::runtime {
namespace {
float coordinate(double value) {
  if (!std::isfinite(value) ||
      std::abs(value) > std::numeric_limits<float>::max()) {
    throw std::invalid_argument(
        "Sampled river curve must fit finite coordinates");
  }
  return static_cast<float>(value);
}
float interpolate(float a, float b, float c, float d, double t) {
  // Uniform Catmull-Rom has no chord-length division, so coincident controls
  // remain well-defined. Endpoint controls are duplicated by the caller.
  const double first = double(c) - a;
  const double second = 2.0 * a - 5.0 * b + 4.0 * c - d;
  const double third = -double(a) + 3.0 * b - 3.0 * c + d;
  return coordinate(double(b) + 0.5 * t * (first + t * (second + t * third)));
}
} // namespace

void validateTerrainWaterPathSettings(const nlohmann::json &parameters) {
  const auto mode =
      parameters.value("river_path_interpolation", std::string("linear"));
  if (mode != "linear" && mode != "catmull_rom") {
    throw std::invalid_argument(
        "River path interpolation must be linear or catmull_rom");
  }
  if (parameters.contains("river_curve_steps")) {
    const auto &steps = parameters.at("river_curve_steps");
    if (!steps.is_number_integer() || steps.get<double>() < 1 ||
        steps.get<double>() > 64) {
      throw std::invalid_argument(
          "River curve steps must be an integer from 1 to 64");
    }
  }
  const bool hasSettings = parameters.contains("river_path_interpolation") ||
                           parameters.contains("river_curve_steps");
  if (hasSettings && !parameters.contains("river_path")) {
    throw std::invalid_argument(
        "River interpolation settings require an explicit river_path");
  }
}

std::vector<Vec3> sampleTerrainWaterPath(std::span<const Vec3> controls,
                                         const nlohmann::json &parameters) {
  validateTerrainWaterPathSettings(parameters);
  for (const auto &point : controls) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
        !std::isfinite(point.z)) {
      throw std::invalid_argument("River control points must be finite");
    }
  }
  if (parameters.value("river_path_interpolation", std::string("linear")) ==
          "linear" ||
      controls.size() < 2) {
    return {controls.begin(), controls.end()};
  }
  const auto steps = parameters.value("river_curve_steps", std::size_t{8});
  std::vector<Vec3> result;
  result.push_back(controls.front());
  for (std::size_t segment = 0; segment + 1 < controls.size(); ++segment) {
    const auto a = controls[segment == 0 ? 0 : segment - 1];
    const auto b = controls[segment];
    const auto c = controls[segment + 1];
    const auto d =
        controls[segment + 2 < controls.size() ? segment + 2 : segment + 1];
    for (std::size_t step = 1; step < steps; ++step) {
      const double t = double(step) / steps;
      result.push_back({interpolate(a.x, b.x, c.x, d.x, t),
                        interpolate(a.y, b.y, c.y, d.y, t),
                        interpolate(a.z, b.z, c.z, d.z, t)});
    }
    result.push_back(c);
  }
  return result;
}
} // namespace demi::runtime
