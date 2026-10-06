#include "demi/assets/TerrainAssetPayloadData.h"

#include "demi/runtime/terrain/TerrainPalette.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace demi::assets::terrain_payload {
namespace {

using Json = nlohmann::json;
using namespace demi::runtime;

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::invalid_argument("Terrain payload: " + std::string(message));
}

float finiteFloat(const Json &value, std::string_view name) {
  require(value.is_number(), std::string(name) + " must be a number");
  const double parsed = value.get<double>();
  require(std::isfinite(parsed) &&
              std::abs(parsed) <= std::numeric_limits<float>::max(),
          std::string(name) + " must be a finite float");
  return static_cast<float>(parsed);
}

std::size_t checkedSize(const Json &value, std::string_view name) {
  require(value.is_number_integer(), std::string(name) + " must be an integer");
  require(!value.is_number_unsigned() ||
              value.get<std::uint64_t>() <=
                  std::numeric_limits<std::size_t>::max(),
          std::string(name) + " is out of range");
  if (value.is_number_unsigned())
    return static_cast<std::size_t>(value.get<std::uint64_t>());
  const auto signedValue = value.get<std::int64_t>();
  require(signedValue >= 0 && static_cast<std::uint64_t>(signedValue) <=
                                  std::numeric_limits<std::size_t>::max(),
          std::string(name) + " is out of range");
  return static_cast<std::size_t>(signedValue);
}

Json vec2(Vec2 value) { return Json::array({value.x, value.y}); }
Json vec3(Vec3 value) { return Json::array({value.x, value.y, value.z}); }

Vec2 readVec2(const Json &value, std::string_view name) {
  require(value.is_array() && value.size() == 2,
          std::string(name) + " must have two coordinates");
  return {finiteFloat(value[0], name), finiteFloat(value[1], name)};
}

Vec3 readVec3(const Json &value, std::string_view name) {
  require(value.is_array() && value.size() == 3,
          std::string(name) + " must have three coordinates");
  return {finiteFloat(value[0], name), finiteFloat(value[1], name),
          finiteFloat(value[2], name)};
}

Json placement(const TerrainScatterPlacement &value) {
  return {
      {"role", std::string(terrainPaletteRoleName(value.role))},
      {"asset", value.asset},
      {"prefab", value.prefab},
      {"biome", value.biome},
      {"biome_name", value.biomeName},
      {"cell", value.cell},
      {"position", vec3(value.position)},
      {"yaw", value.yaw},
      {"scale", value.scale},
      {"collision", std::string(terrainCollisionPolicyName(value.collision))},
      {"lod", value.lod}};
}

TerrainCollisionPolicy readCollision(const Json &value) {
  require(value.is_string(), "collision must be a string");
  const auto name = value.get<std::string>();
  if (name == "none")
    return TerrainCollisionPolicy::None;
  if (name == "static")
    return TerrainCollisionPolicy::Static;
  if (name == "trigger")
    return TerrainCollisionPolicy::Trigger;
  throw std::invalid_argument("Terrain payload: unknown collision policy " +
                              name);
}

TerrainScatterPlacement readPlacement(const Json &value,
                                      const HeightField &field) {
  require(value.is_object(), "placement must be an object");
  TerrainScatterPlacement result;
  require(value.at("role").is_string(), "placement role must be a string");
  const auto role =
      terrainPaletteRoleFromName(value.at("role").get<std::string>());
  require(role.has_value(), "placement has an unknown role");
  result.role = *role;
  result.asset = value.at("asset").get<std::string>();
  result.prefab = value.at("prefab").get<std::string>();
  result.biome = checkedSize(value.at("biome"), "placement biome");
  result.biomeName = value.at("biome_name").get<std::string>();
  result.cell = checkedSize(value.at("cell"), "placement cell");
  result.position = readVec3(value.at("position"), "placement position");
  result.yaw = finiteFloat(value.at("yaw"), "placement yaw");
  result.scale = finiteFloat(value.at("scale"), "placement scale");
  result.collision = readCollision(value.at("collision"));
  result.lod = value.at("lod").get<int>();
  require(result.biome < field.biomeIds.size() &&
              result.biomeName == field.biomeIds[result.biome],
          "placement biome does not match the field");
  require(result.cell < field.heights.size(),
          "placement cell lies outside the field");
  require(result.scale > 0, "placement scale must be positive");
  return result;
}

Json placements(const std::vector<TerrainScatterPlacement> &values) {
  Json result = Json::array();
  for (const auto &value : values)
    result.push_back(placement(value));
  return result;
}

std::vector<TerrainScatterPlacement> readPlacements(const Json &value,
                                                    const HeightField &field) {
  require(value.is_array(), "placements must be an array");
  std::vector<TerrainScatterPlacement> result;
  result.reserve(value.size());
  for (const auto &entry : value)
    result.push_back(readPlacement(entry, field));
  return result;
}

Json palette(const TerrainPalette &value) {
  Json roles = Json::object();
  for (const auto &[name, entry] : value.roles) {
    roles[name] = {
        {"asset", entry.asset},
        {"prefab", entry.prefab},
        {"weight", entry.weight},
        {"scale_min", entry.scaleMin},
        {"scale_max", entry.scaleMax},
        {"spacing", entry.spacing},
        {"collision", std::string(terrainCollisionPolicyName(entry.collision))},
        {"lod", entry.lod},
        {"biomes", entry.biomes}};
  }
  return {{"format_version", value.formatVersion},
          {"id", value.id},
          {"name", value.name},
          {"roles", std::move(roles)},
          {"required_roles", value.requiredRoles}};
}

TerrainPalette readPalette(const Json &value) {
  require(value.is_object(), "palette must be an object");
  TerrainPalette result;
  result.formatVersion = value.at("format_version").get<int>();
  require(result.formatVersion == 1, "palette format is unsupported");
  result.id = value.at("id").get<std::string>();
  result.name = value.at("name").get<std::string>();
  result.requiredRoles =
      value.at("required_roles").get<std::vector<std::string>>();
  const auto &roles = value.at("roles");
  require(roles.is_object(), "palette roles must be an object");
  for (const auto &[name, item] : roles.items()) {
    const auto role = terrainPaletteRoleFromName(name);
    require(role.has_value(), "palette has an unknown role");
    TerrainPaletteEntry entry;
    entry.role = *role;
    entry.asset = item.at("asset").get<std::string>();
    entry.prefab = item.at("prefab").get<std::string>();
    entry.weight = finiteFloat(item.at("weight"), "palette weight");
    entry.scaleMin = finiteFloat(item.at("scale_min"), "palette minimum scale");
    entry.scaleMax = finiteFloat(item.at("scale_max"), "palette maximum scale");
    entry.spacing = finiteFloat(item.at("spacing"), "palette spacing");
    entry.collision = readCollision(item.at("collision"));
    entry.lod = item.at("lod").get<int>();
    entry.biomes = item.at("biomes").get<std::vector<std::string>>();
    require(entry.weight >= 0 && entry.scaleMin > 0 &&
                entry.scaleMin <= entry.scaleMax && entry.spacing >= 0,
            "palette contains invalid placement dimensions");
    result.roles.emplace(name, std::move(entry));
  }
  return result;
}

std::string_view waterKind(TerrainWaterBody kind) {
  switch (kind) {
  case TerrainWaterBody::Ocean:
    return "ocean";
  case TerrainWaterBody::Lake:
    return "lake";
  case TerrainWaterBody::River:
    return "river";
  }
  throw std::invalid_argument("Terrain payload: unknown water body kind");
}

TerrainWaterBody readWaterKind(const Json &value) {
  require(value.is_string(), "water kind must be a string");
  const auto name = value.get<std::string>();
  if (name == "ocean")
    return TerrainWaterBody::Ocean;
  if (name == "lake")
    return TerrainWaterBody::Lake;
  if (name == "river")
    return TerrainWaterBody::River;
  throw std::invalid_argument("Terrain payload: unknown water kind " + name);
}

Json waterBody(const TerrainWaterBodySpec &value) {
  Json path = Json::array();
  for (const auto &point : value.riverPath)
    path.push_back(vec3(point));
  return {{"id", value.id},
          {"kind", std::string(waterKind(value.kind))},
          {"level", value.level},
          {"river_path", std::move(path)},
          {"river_width", value.riverWidth},
          {"center", vec2(value.center)},
          {"radius", value.radius},
          {"shoreline_softening", value.shorelineSoftening},
          {"appearance", terrainWaterAppearanceJson(value.appearance)}};
}

TerrainWaterBodySpec readWaterBody(const Json &value) {
  TerrainWaterBodySpec result;
  result.id = value.at("id").get<std::string>();
  require(!result.id.empty(), "water body id must not be empty");
  result.kind = readWaterKind(value.at("kind"));
  result.level = finiteFloat(value.at("level"), "water level");
  const auto &path = value.at("river_path");
  require(path.is_array(), "river path must be an array");
  for (const auto &point : path)
    result.riverPath.push_back(readVec3(point, "river path point"));
  result.riverWidth = finiteFloat(value.at("river_width"), "river width");
  result.center = readVec2(value.at("center"), "water center");
  result.radius = finiteFloat(value.at("radius"), "water radius");
  result.shorelineSoftening = value.at("shoreline_softening").get<bool>();
  result.appearance = parseTerrainWaterAppearance(value.at("appearance"));
  require(result.riverWidth >= 0 && result.radius >= 0,
          "water body dimensions must be nonnegative");
  return result;
}

Json waterAuthoring(const TerrainWaterAuthoring &value) {
  Json bodies = Json::array();
  for (const auto &body : value.bodies)
    bodies.push_back(waterBody(body));
  return {{"bodies", std::move(bodies)},
          {"sea_level", value.seaLevel},
          {"authored", value.authored}};
}

TerrainWaterAuthoring readWaterAuthoring(const Json &value) {
  TerrainWaterAuthoring result;
  result.seaLevel = finiteFloat(value.at("sea_level"), "sea level");
  result.authored = value.at("authored").get<bool>();
  const auto &bodies = value.at("bodies");
  require(bodies.is_array(), "water bodies must be an array");
  for (const auto &body : bodies)
    result.bodies.push_back(readWaterBody(body));
  return result;
}

std::size_t waterCoverageSampleCount(const HeightField &field) {
  require(field.cellsX > 0 && field.cellsZ > 0 && std::isfinite(field.size.x) &&
              std::isfinite(field.size.y) && field.size.x > 0 &&
              field.size.y > 0,
          "water coverage requires a finite positive terrain grid");
  const auto columns = std::size_t(field.cellsX) + 1;
  const auto rows = std::size_t(field.cellsZ) + 1;
  require(columns <= std::numeric_limits<std::size_t>::max() / rows,
          "water coverage grid exceeds host size");
  const auto count = columns * rows;
  require(count == field.heights.size(),
          "water coverage terrain samples must match the grid");
  return count;
}

void validateWaterCoverage(
    const terrain_water_detail::WaterLevelField &coverage,
    const HeightField &field, const TerrainWaterAuthoring &authoring) {
  const auto count = waterCoverageSampleCount(field);
  require(coverage.size.x == field.size.x && coverage.size.y == field.size.y &&
              coverage.cellsX == field.cellsX &&
              coverage.cellsZ == field.cellsZ,
          "water coverage dimensions must match the terrain grid");
  require(coverage.level.size() == count && coverage.wet.size() == count &&
              coverage.body.size() == count,
          "water coverage arrays must match the terrain grid");
  std::vector<std::uint8_t> accepted(authoring.bodies.size());
  for (const auto body : terrain_water_detail::acceptedBodies(authoring))
    accepted[body] = 1;
  for (std::size_t sample = 0; sample < count; ++sample) {
    require(std::isfinite(coverage.level[sample]),
            "water coverage levels must be finite");
    require(coverage.wet[sample] <= 1,
            "water coverage wet flags must be zero or one");
    const auto body = coverage.body[sample];
    const bool hasBody = body != terrain_water_detail::WaterLevelField::noBody;
    require(bool(coverage.wet[sample]) == hasBody,
            "water coverage wet flags must match body ownership");
    if (!hasBody)
      continue;
    require(body < authoring.bodies.size(),
            "water coverage body index lies outside authoring");
    require(accepted[body] != 0,
            "water coverage body index references a rejected body");
    require(coverage.level[sample] == authoring.bodies[body].level,
            "water coverage level must match its authored body");
  }
}

Json waterCoverage(const TerrainWaterResult &value, const HeightField &field,
                   const TerrainWaterAuthoring &authoring) {
  if (!value.resolvedCoverage)
    return nullptr;
  const auto &coverage = *value.resolvedCoverage;
  validateWaterCoverage(coverage, field, authoring);
  Json bodies = Json::array();
  for (const auto body : coverage.body) {
    if (body == terrain_water_detail::WaterLevelField::noBody)
      bodies.push_back(-1);
    else
      bodies.push_back(body);
  }
  // Grid dimensions come from the terrain; flags and levels come from owners.
  // Ownership includes dry shore samples and must survive without re-solving.
  return {{"body", std::move(bodies)}};
}

std::shared_ptr<const terrain_water_detail::WaterLevelField>
readWaterCoverage(const Json &value, const HeightField &field,
                  const TerrainWaterAuthoring &authoring) {
  if (value.is_null())
    return nullptr;
  require(value.is_object(), "water coverage must be an object or null");
  const auto count = waterCoverageSampleCount(field);
  const auto &bodies = value.at("body");
  require(bodies.is_array() && bodies.size() == count,
          "water coverage body array must match the terrain grid");
  auto coverage = std::make_shared<terrain_water_detail::WaterLevelField>();
  coverage->size = field.size;
  coverage->cellsX = field.cellsX;
  coverage->cellsZ = field.cellsZ;
  coverage->level.assign(count, 0.F);
  coverage->wet.assign(count, 0);
  coverage->body.assign(count, terrain_water_detail::WaterLevelField::noBody);
  for (std::size_t sample = 0; sample < count; ++sample) {
    const auto &entry = bodies[sample];
    if (entry.is_number_integer() && !entry.is_number_unsigned() &&
        entry.get<std::int64_t>() == -1)
      continue;
    const auto body = checkedSize(entry, "water coverage body index");
    require(body < authoring.bodies.size(),
            "water coverage body index lies outside authoring");
    coverage->body[sample] = body;
    coverage->wet[sample] = 1;
    coverage->level[sample] = authoring.bodies[body].level;
  }
  validateWaterCoverage(*coverage, field, authoring);
  return coverage;
}

Json waterResult(const TerrainWaterResult &value, const HeightField &field,
                 const TerrainWaterAuthoring &authoring) {
  Json surfaces = Json::array();
  Json carvedHeights = Json::array();
  for (float height : value.carvedHeights)
    carvedHeights.push_back(height);
  for (const auto &surface : value.surfaces) {
    Json vertices = Json::array(), normals = Json::array();
    for (const auto &point : surface.vertices)
      vertices.push_back(vec3(point));
    for (const auto &normal : surface.normals)
      normals.push_back(vec3(normal));
    surfaces.push_back({{"id", surface.id},
                        {"kind", std::string(waterKind(surface.kind))},
                        {"level", surface.level},
                        {"vertices", std::move(vertices)},
                        {"indices", surface.indices},
                        {"normals", std::move(normals)},
                        {"depth", surface.depth}});
  }
  return {{"resolved_coverage", waterCoverage(value, field, authoring)},
          {"carved_heights", std::move(carvedHeights)},
          {"surfaces", std::move(surfaces)},
          {"dropped", value.dropped}};
}

TerrainWaterResult readWaterResult(const Json &value,
                                   const HeightField &field,
                                   const TerrainWaterAuthoring &authoring) {
  TerrainWaterResult result;
  result.resolvedCoverage =
      readWaterCoverage(value.at("resolved_coverage"), field, authoring);
  const auto &heights = value.at("carved_heights");
  require(heights.is_array() && heights.size() == field.heights.size(),
          "carved water heights must match the terrain grid");
  result.carvedHeights.resize(heights.size());
  for (std::size_t index = 0; index < heights.size(); ++index)
    result.carvedHeights.set(index,
                             finiteFloat(heights[index], "carved height"));
  const auto &surfaces = value.at("surfaces");
  require(surfaces.is_array(), "water surfaces must be an array");
  for (const auto &entry : surfaces) {
    TerrainWaterSurface surface;
    surface.id = entry.at("id").get<std::string>();
    surface.kind = readWaterKind(entry.at("kind"));
    surface.level = finiteFloat(entry.at("level"), "water surface level");
    const auto &vertices = entry.at("vertices");
    const auto &normals = entry.at("normals");
    const auto &depth = entry.at("depth");
    const auto &indices = entry.at("indices");
    require(vertices.is_array() && normals.is_array() && depth.is_array() &&
                indices.is_array() && normals.size() == vertices.size() &&
                depth.size() == vertices.size(),
            "water surface arrays disagree");
    surface.vertices.reserve(vertices.size());
    surface.normals.reserve(vertices.size());
    surface.depth.reserve(vertices.size());
    for (std::size_t index = 0; index < vertices.size(); ++index) {
      surface.vertices.push_back(readVec3(vertices[index], "water vertex"));
      surface.normals.push_back(readVec3(normals[index], "water normal"));
      surface.depth.push_back(finiteFloat(depth[index], "water depth"));
      require(surface.depth.back() >= 0, "water depth must be nonnegative");
    }
    for (const auto &index : indices) {
      const auto position = checkedSize(index, "water index");
      require(position < surface.vertices.size(),
              "water index lies outside its vertex array");
      surface.indices.push_back(position);
    }
    require(surface.indices.size() % 3 == 0,
            "water indices must describe triangles");
    result.surfaces.push_back(std::move(surface));
  }
  result.dropped = checkedSize(value.at("dropped"), "dropped water bodies");
  return result;
}

} // namespace

Json encodeDerived(const HeightField &field) {
  Json result{{"format_version", 1},
              {"scatter", placements(field.scatterPlacements)},
              {"scatter_truncated", field.scatterTruncated},
              {"palette", field.resolvedPalette
                              ? palette(*field.resolvedPalette)
                              : Json(nullptr)},
              {"graph", nullptr}};
  if (field.graphArtifacts) {
    const auto &artifacts = *field.graphArtifacts;
    Json runs = Json::array();
    for (const auto &run : artifacts.nodes)
      runs.push_back({{"id", run.id},
                      {"cached", run.cached},
                      {"milliseconds", run.milliseconds}});
    result["graph"] = {
        {"candidates", artifacts.basePlacements
                           ? placements(*artifacts.basePlacements)
                           : Json(nullptr)},
        {"water_authoring", waterAuthoring(artifacts.water)},
        {"water_result", artifacts.waterResult
                             ? waterResult(*artifacts.waterResult, field,
                                           artifacts.water)
                             : Json(nullptr)},
        {"runs", std::move(runs)},
        {"warnings", artifacts.warnings}};
  }
  return result;
}

void decodeDerived(const Json &document, HeightField &field) {
  require(document.is_object() && document.at("format_version") == 1,
          "unsupported derived-data format");
  field.scatterPlacements = readPlacements(document.at("scatter"), field);
  field.scatterTruncated = document.at("scatter_truncated").get<bool>();
  if (!document.at("palette").is_null()) {
    auto resolved = readPalette(document.at("palette"));
    require(resolved.id == field.paletteId,
            "resolved palette id differs from cooked field");
    field.resolvedPalette =
        std::make_shared<const TerrainPalette>(std::move(resolved));
  } else {
    require(field.paletteId.empty(), "cooked field lost its resolved palette");
  }
  const auto &graph = document.at("graph");
  if (!graph.is_null()) {
    auto artifacts = std::make_shared<TerrainGraphArtifacts>();
    if (!graph.at("candidates").is_null())
      artifacts->basePlacements =
          std::make_shared<const std::vector<TerrainScatterPlacement>>(
              readPlacements(graph.at("candidates"), field));
    artifacts->water = readWaterAuthoring(graph.at("water_authoring"));
    if (!graph.at("water_result").is_null())
      artifacts->waterResult =
          readWaterResult(graph.at("water_result"), field, artifacts->water);
    for (const auto &run : graph.at("runs")) {
      TerrainGraphNodeRun decoded;
      decoded.id = run.at("id").get<std::string>();
      decoded.cached = run.at("cached").get<bool>();
      const double elapsed = run.at("milliseconds").get<double>();
      require(std::isfinite(elapsed) && elapsed >= 0,
              "graph run time must be finite and nonnegative");
      decoded.milliseconds = elapsed;
      artifacts->nodes.push_back(std::move(decoded));
    }
    artifacts->warnings = graph.at("warnings").get<std::vector<std::string>>();
    field.graphArtifacts = std::move(artifacts);
  }
}

} // namespace demi::assets::terrain_payload
