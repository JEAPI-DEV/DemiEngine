#include "demi/assets/ColliderShapeAsset.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace demi::assets {
namespace {
using Vector = std::array<double, 3>;
Vector subtract(const std::array<float, 3> &a, const std::array<float, 3> &b) {
  return {double(a[0]) - b[0], double(a[1]) - b[1], double(a[2]) - b[2]};
}
Vector cross(const Vector &a, const Vector &b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
double dot(const Vector &a, const Vector &b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
} // namespace

std::optional<ColliderShapeAsset>
parseColliderShapeAsset(const nlohmann::json &document, std::string &error) {
  error = "Collider source requires format_version 1, shape convex_hull, and "
          "4..256 finite, non-coplanar points.";
  try {
    if (document.is_object() && document.value("shape", "") == "compound") {
      error = "Compound collider requires format_version 1 and 1..256 convex "
              "parts with unique IDs.";
      if (!document.contains("format_version") ||
          !document["format_version"].is_number_integer() ||
          document.value("format_version", 0) != 1 ||
          document.contains("points") || !document.contains("parts") ||
          !document["parts"].is_array() || document["parts"].empty() ||
          document["parts"].size() > 256)
        return std::nullopt;
      ColliderShapeAsset result;
      result.minimum.fill(std::numeric_limits<float>::max());
      result.maximum.fill(std::numeric_limits<float>::lowest());
      std::unordered_set<std::string> ids;
      for (const auto &part : document["parts"]) {
        if (!part.is_object() || part.size() != 2 || !part.contains("id") ||
            !part["id"].is_string() || !part.contains("points"))
          return std::nullopt;
        const auto id = part["id"].get<std::string>();
        const auto alphanumeric = [](char c) {
          return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9');
        };
        if (id.empty() || id.size() > 128 || !alphanumeric(id.front()) ||
            !std::ranges::all_of(id,
                                 [&](char c) {
                                   return alphanumeric(c) || c == '_' ||
                                          c == '-' || c == '.';
                                 }) ||
            !ids.insert(id).second)
          return std::nullopt;
        std::string partError;
        auto hull = parseColliderShapeAsset({{"format_version", 1},
                                             {"shape", "convex_hull"},
                                             {"points", part["points"]}},
                                            partError);
        if (!hull) {
          error = "Compound part " + id + ": " + partError;
          return std::nullopt;
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
          result.minimum[axis] =
              std::min(result.minimum[axis], hull->minimum[axis]);
          result.maximum[axis] =
              std::max(result.maximum[axis], hull->maximum[axis]);
          if (double(result.maximum[axis]) - result.minimum[axis] >
              std::numeric_limits<float>::max())
            return std::nullopt;
        }
        result.parts.push_back({id, std::move(hull->points)});
      }
      if (document.contains("fracture")) {
        std::vector<std::string> ids;
        for (const auto &part : result.parts)
          ids.push_back(part.id);
        result.fracture =
            parseColliderFractureGraph(document["fracture"], ids, error);
        if (!result.fracture)
          return std::nullopt;
      }
      error.clear();
      return result;
    }
    if (!document.is_object() || !document.contains("format_version") ||
        !document["format_version"].is_number_integer() ||
        document.value("format_version", 0) != 1 ||
        document.value("shape", "") != "convex_hull" ||
        document.contains("parts") || document.contains("fracture") ||
        !document.contains("points") ||
        !document["points"].is_array() || document["points"].size() < 4 ||
        document["points"].size() > 256)
      return std::nullopt;
    ColliderShapeAsset result;
    result.minimum.fill(std::numeric_limits<float>::max());
    result.maximum.fill(std::numeric_limits<float>::lowest());
    for (const auto &entry : document["points"]) {
      if (!entry.is_array() || entry.size() != 3)
        return std::nullopt;
      std::array<float, 3> point{};
      for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!entry[axis].is_number())
          return std::nullopt;
        point[axis] = entry[axis].get<float>();
        if (!std::isfinite(point[axis]))
          return std::nullopt;
        result.minimum[axis] = std::min(result.minimum[axis], point[axis]);
        result.maximum[axis] = std::max(result.maximum[axis], point[axis]);
      }
      result.points.push_back(point);
    }
    double extent = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const double size = double(result.maximum[axis]) - result.minimum[axis];
      if (size > std::numeric_limits<float>::max())
        return std::nullopt;
      extent = std::max(extent, size);
    }
    if (extent <= 0)
      return std::nullopt;
    const double tolerance = extent * 1e-6;
    Vector edge{};
    double edgeLengthSquared = 0;
    for (const auto &point : result.points) {
      const auto candidate = subtract(point, result.points.front());
      if (dot(candidate, candidate) > edgeLengthSquared) {
        edge = candidate;
        edgeLengthSquared = dot(candidate, candidate);
      }
    }
    Vector normal{};
    double normalLengthSquared = 0;
    for (const auto &point : result.points) {
      const auto candidate =
          cross(edge, subtract(point, result.points.front()));
      if (dot(candidate, candidate) > normalLengthSquared) {
        normal = candidate;
        normalLengthSquared = dot(candidate, candidate);
      }
    }
    if (normalLengthSquared <= tolerance * tolerance * edgeLengthSquared)
      return std::nullopt;
    const double planeTolerance = tolerance * std::sqrt(normalLengthSquared);
    for (const auto &point : result.points)
      if (std::abs(dot(normal, subtract(point, result.points.front()))) >
          planeTolerance) {
        error.clear();
        return result;
      }
  } catch (const nlohmann::json::exception &exception) {
    error += " ";
    error += exception.what();
  }
  return std::nullopt;
}

std::optional<ColliderShapeAsset>
loadColliderShapeAsset(const std::filesystem::path &path, std::string &error) {
  std::ifstream input(path);
  if (!input) {
    error = "Could not read collider source: " + path.string();
    return std::nullopt;
  }
  const auto document = nlohmann::json::parse(input, nullptr, false);
  return parseColliderShapeAsset(document, error);
}
} // namespace demi::assets
