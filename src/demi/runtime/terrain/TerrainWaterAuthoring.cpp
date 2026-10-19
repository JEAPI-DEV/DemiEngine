#include "demi/runtime/terrain/TerrainWaterAuthoring.h"

#include "demi/runtime/terrain/TerrainPolygon.h"
#include "demi/runtime/terrain/TerrainWaterFlow.h"
#include "demi/runtime/terrain/TerrainWaterPath.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string_view>

namespace demi::runtime {
namespace {
std::vector<Vec2> readBoundary(const nlohmann::json &value) {
  if (!value.is_array()) {
    throw std::invalid_argument("Water boundary must be an array of XZ points");
  }
  std::vector<Vec2> result;
  for (const auto &point : value) {
    if (!point.is_array() || point.size() != 2 || !point[0].is_number() ||
        !point[1].is_number()) {
      throw std::invalid_argument("Water boundary requires numeric XZ pairs");
    }
    result.push_back({point[0].get<float>(), point[1].get<float>()});
  }
  if (!result.empty()) {
    validateTerrainPolygon(result);
  }
  return result;
}

const std::set<std::string, std::less<>> BodyParameterNames{
    "kind",
    "level",
    "center_x",
    "center_z",
    "radius",
    "boundary",
    "river_width",
    "river_depth",
    "river_surface_mode",
    "river_path",
    "river_path_interpolation",
    "river_curve_steps",
    "flow_speed",
    "flow_direction",
    "shoreline_softening",
    "shallow_color",
    "deep_color",
    "absorption_distance",
    "roughness",
    "wave_amplitude",
    "wave_length",
    "wave_speed",
    "normal_strength",
    "foam_width",
    "foam_strength",
    "foam_color"};
} // namespace

void validateTerrainWaterBody(const TerrainWaterBodySpec &body,
                              std::stop_token stop) {
  if (body.id.empty()) {
    throw std::invalid_argument("Water body requires a stable ID");
  }
  if (body.kind != TerrainWaterBody::Lake &&
      body.kind != TerrainWaterBody::Ocean &&
      body.kind != TerrainWaterBody::River) {
    throw std::invalid_argument("Unknown water body kind");
  }
  if (!std::isfinite(body.level) || !std::isfinite(body.center.x) ||
      !std::isfinite(body.center.y) || !std::isfinite(body.radius) ||
      body.radius < 0) {
    throw std::invalid_argument(
        "Water level/center/radius must be finite; radius cannot be negative");
  }
  if (!std::isfinite(body.riverDepth) || body.riverDepth <= 0 ||
      (body.riverSurfaceMode != TerrainRiverSurfaceMode::Level &&
       body.riverSurfaceMode != TerrainRiverSurfaceMode::FollowBed) ||
      (body.kind != TerrainWaterBody::River &&
       body.riverSurfaceMode != TerrainRiverSurfaceMode::Level)) {
    throw std::invalid_argument(
        "Water river mode/depth does not match its body kind");
  }
  validateTerrainWaterAppearance(body.appearance);
  validateTerrainWaterFlow(body);
  if (!body.boundary.empty()) {
    if (body.kind == TerrainWaterBody::River) {
      throw std::invalid_argument(
          "River boundaries use a path and width, not a polygon");
    }
    validateTerrainPolygon(body.boundary, stop);
    if (body.kind == TerrainWaterBody::Lake &&
        !terrainPolygonContains(body.boundary, body.center)) {
      throw std::invalid_argument(
          "Lake center must lie inside its polygon boundary");
    }
  }
  if (body.kind == TerrainWaterBody::River) {
    if (body.riverPath.size() < 2 || !std::isfinite(body.riverWidth) ||
        body.riverWidth <= 0) {
      throw std::invalid_argument(
          "River needs a positive width and at least two path points");
    }
    double length = 0;
    auto previous = body.riverPath.front();
    for (const auto &point : body.riverPath) {
      if (stop.stop_requested()) {
        return;
      }
      if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
          !std::isfinite(point.z) ||
          (body.riverSurfaceMode == TerrainRiverSurfaceMode::FollowBed &&
           !std::isfinite(point.y + body.riverDepth))) {
        throw std::invalid_argument(
            "River path must fit finite terrain coordinates");
      }
      length += std::hypot(double(point.x) - previous.x,
                           double(point.z) - previous.z);
      previous = point;
    }
    if (!(length > 0)) {
      throw std::invalid_argument("River path has no horizontal extent");
    }
  }
}

TerrainWaterBodySpec
parseTerrainWaterBodyParameters(const nlohmann::json &parameters,
                                std::string id,
                                const TerrainWaterBodySpec &defaults) {
  if (!parameters.is_object()) {
    throw std::invalid_argument("Water body parameters must be an object");
  }
  for (const auto &[name, value] : parameters.items()) {
    (void)value;
    if (!BodyParameterNames.contains(name)) {
      throw std::invalid_argument("Unknown water body parameter: " + name);
    }
  }
  validateTerrainWaterPathSettings(parameters);
  auto body = defaults;
  body.id = std::move(id);
  if (parameters.contains("kind")) {
    const auto kind = parameters.at("kind").get<std::string>();
    if (kind != "lake" && kind != "ocean" && kind != "river") {
      throw std::invalid_argument("Water kind must be lake, ocean or river");
    }
    body.kind = kind == "river"  ? TerrainWaterBody::River
                : kind == "lake" ? TerrainWaterBody::Lake
                                 : TerrainWaterBody::Ocean;
  }
  body.level = parameters.value("level", body.level);
  body.center.x = parameters.value("center_x", body.center.x);
  body.center.y = parameters.value("center_z", body.center.y);
  body.radius = parameters.value("radius", body.radius);
  body.riverWidth = parameters.value("river_width", body.riverWidth);
  body.riverDepth = parameters.value("river_depth", body.riverDepth);
  body.shorelineSoftening =
      parameters.value("shoreline_softening", body.shorelineSoftening);
  if (parameters.contains("river_surface_mode")) {
    body.riverSurfaceMode = parseTerrainRiverSurfaceMode(
        parameters.at("river_surface_mode").get<std::string>());
  }
  if (parameters.contains("boundary")) {
    body.boundary = readBoundary(parameters.at("boundary"));
  }
  if (parameters.contains("river_path")) {
    const auto &path = parameters.at("river_path");
    if (!path.is_array()) {
      throw std::invalid_argument("River path must be an array of XYZ points");
    }
    body.riverPath.clear();
    for (const auto &point : path) {
      if (!point.is_array() || point.size() != 3 || !point[0].is_number() ||
          !point[1].is_number() || !point[2].is_number()) {
        throw std::invalid_argument("River path requires numeric XYZ triples");
      }
      body.riverPath.push_back({point[0].get<float>(), point[1].get<float>(),
                                point[2].get<float>()});
    }
  }
  if (parameters.contains("river_path")) {
    body.riverPath = sampleTerrainWaterPath(body.riverPath, parameters);
  }
  parseTerrainWaterFlow(parameters, body);
  auto appearance = terrainWaterAppearanceJson(body.appearance);
  for (auto &[name, value] : appearance.items()) {
    if (parameters.contains(name)) {
      value = parameters.at(name);
    }
  }
  body.appearance = parseTerrainWaterAppearance(appearance);
  validateTerrainWaterBody(body);
  return body;
}

void validateTerrainWaterEdits(const nlohmann::json &excludedIds,
                               const nlohmann::json &overrides) {
  if (!excludedIds.is_array() || !overrides.is_array()) {
    throw std::invalid_argument(
        "Water exclusions and overrides must be arrays");
  }
  std::set<std::string> excluded;
  for (const auto &value : excludedIds) {
    if (!value.is_string() || value.get<std::string>().empty() ||
        !excluded.insert(value.get<std::string>()).second) {
      throw std::invalid_argument(
          "Water exclusions need unique nonempty body IDs");
    }
  }
  std::set<std::string> edited;
  for (const auto &authoredOverride : overrides) {
    if (!authoredOverride.is_object() || !authoredOverride.contains("id") ||
        !authoredOverride.at("id").is_string()) {
      throw std::invalid_argument("Water overrides require a stable string id");
    }
    const auto id = authoredOverride.at("id").get<std::string>();
    if (id.empty() || !edited.insert(id).second || excluded.contains(id)) {
      throw std::invalid_argument(
          "Water override IDs must be unique and cannot also be excluded");
    }
    auto parameters = authoredOverride;
    parameters.erase("id");
    // Cross-field validation follows merging with the actual body. Scalar and
    // structured domains can still be checked before upstream IDs resolve.
    for (const auto &[name, value] : parameters.items()) {
      (void)value;
      if (!BodyParameterNames.contains(name)) {
        throw std::invalid_argument("Unknown water override parameter: " +
                                    name);
      }
    }
    for (const auto *name : {"level", "center_x", "center_z", "radius",
                             "river_width", "river_depth"}) {
      if (!parameters.contains(name)) {
        continue;
      }
      const auto &value = parameters.at(name);
      if (!value.is_number() || !std::isfinite(value.get<float>())) {
        throw std::invalid_argument(
            std::string("Water override needs finite ") + name);
      }
      const float number = value.get<float>();
      if ((std::string_view(name) == "radius" && number < 0) ||
          ((std::string_view(name) == "river_width" ||
            std::string_view(name) == "river_depth") &&
           number <= 0)) {
        throw std::invalid_argument(
            std::string("Water override is outside the domain for ") + name);
      }
    }
    validateTerrainWaterPathSettings(parameters);
    TerrainWaterBodySpec flowProbe;
    flowProbe.kind = TerrainWaterBody::River;
    parseTerrainWaterFlow(parameters, flowProbe);
    (void)parseTerrainWaterAppearance(parameters);
    if (parameters.contains("kind")) {
      const auto kind = parameters.at("kind").get<std::string>();
      if (kind != "river" && kind != "lake" && kind != "ocean") {
        throw std::invalid_argument("Unknown overridden water kind");
      }
    }
    if (parameters.contains("river_surface_mode")) {
      (void)parseTerrainRiverSurfaceMode(
          parameters.at("river_surface_mode").get<std::string>());
    }
    if (parameters.contains("shoreline_softening") &&
        !parameters.at("shoreline_softening").is_boolean()) {
      throw std::invalid_argument(
          "Water shoreline softening must be a boolean");
    }
    if (parameters.contains("boundary")) {
      (void)readBoundary(parameters.at("boundary"));
    }
    if (parameters.contains("river_path")) {
      const auto &path = parameters.at("river_path");
      if (!path.is_array()) {
        throw std::invalid_argument("Overridden river path must be an array");
      }
      for (const auto &point : path) {
        if (!point.is_array() || point.size() != 3) {
          throw std::invalid_argument(
              "Overridden river path needs XYZ triples");
        }
        for (const auto &coordinate : point) {
          if (!coordinate.is_number() ||
              !std::isfinite(coordinate.get<float>())) {
            throw std::invalid_argument(
                "Overridden river path coordinates must be finite");
          }
        }
      }
    }
  }
}

TerrainWaterAuthoring
editTerrainWaterAuthoring(const TerrainWaterAuthoring &source,
                          std::span<const std::string> excludedIds,
                          const nlohmann::json &overrides) {
  nlohmann::json exclusions = nlohmann::json::array();
  for (const auto &id : excludedIds) {
    exclusions.push_back(id);
  }
  validateTerrainWaterEdits(exclusions, overrides);
  auto result = source;
  result.authored = true;
  for (const auto &id : excludedIds) {
    const auto found =
        std::ranges::find(result.bodies, id, &TerrainWaterBodySpec::id);
    if (found == result.bodies.end()) {
      throw std::invalid_argument("Excluded water body does not exist: " + id);
    }
    result.bodies.erase(found);
  }
  for (const auto &authoredOverride : overrides) {
    const auto id = authoredOverride.at("id").get<std::string>();
    const auto found =
        std::ranges::find(result.bodies, id, &TerrainWaterBodySpec::id);
    if (found == result.bodies.end()) {
      throw std::invalid_argument("Overridden water body does not exist: " +
                                  id);
    }
    auto parameters = authoredOverride;
    parameters.erase("id");
    *found = parseTerrainWaterBodyParameters(parameters, id, *found);
  }
  return result;
}
} // namespace demi::runtime
