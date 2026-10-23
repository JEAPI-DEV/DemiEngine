#include "demi/assets/TerrainAssetPayloadData.h"
#include "demi/runtime/terrain/TerrainWaterFlow.h"

#include "demi/runtime/terrain/TerrainPalette.h"
#include "demi/runtime/terrain/TerrainSurfaceMaterials.h"
#include "demi/assets/MaterialSet.h"

#include <algorithm>
#include <set>
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

Json transportLedger(const TerrainErosionTransport &transport) {
  for (const double value : {transport.exportedSediment,
                             transport.suspendedSediment,
                             transport.erodedSediment,
                             transport.depositedSediment}) {
    require(std::isfinite(value) && value >= 0,
            "hydraulic transport totals must be finite and nonnegative");
  }
  return {{"exported", transport.exportedSediment},
          {"suspended", transport.suspendedSediment},
          {"eroded", transport.erodedSediment},
          {"deposited", transport.depositedSediment}};
}

TerrainErosionTransport readTransportLedger(const Json &document) {
  require(document.is_object(), "hydraulic transport ledger must be an object");
  TerrainErosionTransport transport;
  transport.exportedSediment = document.at("exported").get<double>();
  transport.suspendedSediment = document.at("suspended").get<double>();
  transport.erodedSediment = document.at("eroded").get<double>();
  transport.depositedSediment = document.at("deposited").get<double>();
  (void)transportLedger(transport);
  return transport;
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
      {"rule_id", std::string(value.ruleId)},
      {"model", value.model},
      {"prefab", value.prefab},
      {"biome", value.biome},
      {"biome_name", value.biomeName},
      {"cell", value.cell},
      {"candidate_x", value.candidateX},
      {"candidate_z", value.candidateZ},
      {"position", vec3(value.position)},
      {"yaw", value.yaw},
      {"scale", value.scale},
      {"collision", std::string(terrainCollisionPolicyName(value.collision))}};
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
  require(value.at("rule_id").is_string(), "placement ruleId must be a string");
  result.ruleId = value.at("rule_id").get<std::string>();
  require(validTerrainPlacementRuleId(result.ruleId),
          "placement has an invalid rule ID");
  result.model = value.at("model").get<std::string>();
  result.prefab = value.at("prefab").get<std::string>();
  require(result.model.empty() != result.prefab.empty(),
          "placement requires exactly one model or prefab");
  result.biome = checkedSize(value.at("biome"), "placement biome");
  result.biomeName = value.at("biome_name").get<std::string>();
  result.cell = checkedSize(value.at("cell"), "placement cell");
  const auto candidateX =
      checkedSize(value.at("candidate_x"), "placement candidate X");
  const auto candidateZ =
      checkedSize(value.at("candidate_z"), "placement candidate Z");
  require(candidateX <= std::size_t(std::numeric_limits<int>::max()) &&
              candidateZ <= std::size_t(std::numeric_limits<int>::max()),
          "placement candidate coordinates are out of range");
  result.candidateX = static_cast<int>(candidateX);
  result.candidateZ = static_cast<int>(candidateZ);
  result.position = readVec3(value.at("position"), "placement position");
  result.yaw = finiteFloat(value.at("yaw"), "placement yaw");
  result.scale = finiteFloat(value.at("scale"), "placement scale");
  result.collision = readCollision(value.at("collision"));
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
  Json rules = Json::object();
  for (const auto &[name, entry] : value.placements) {
    rules[name] = {
        {"model", entry.model},
        {"prefab", entry.prefab},
        {"density", entry.density},
        {"candidate_spacing", entry.candidateSpacing},
        {"position_jitter", entry.positionJitter},
        {"category", terrainScatterCategoryName(entry.category)},
        {"slope", {entry.slope.minimum, entry.slope.maximum}},
        {"moisture", {entry.moisture.minimum, entry.moisture.maximum}},
        {"water_distance",
         {entry.waterDistance.minimum, entry.waterDistance.maximum}},
        {"yaw", {entry.yawDegrees.minimum, entry.yawDegrees.maximum}},
        {"cluster_size", entry.clusterSize},
        {"cluster_strength", entry.clusterStrength},
        {"scale_min", entry.scaleMin},
        {"scale_max", entry.scaleMax},
        {"spacing", entry.spacing},
        {"collision", std::string(terrainCollisionPolicyName(entry.collision))},
        {"biomes", entry.biomes}};
  }
  return {{"format_version", value.formatVersion},
          {"id", value.id},
          {"name", value.name},
          {"art_density", value.artDensity},
          {"grass_density", value.grassDensity},
          {"tree_density", value.treeDensity},
          {"rock_density", value.rockDensity},
          {"placements", std::move(rules)},
          {"material_set", value.materialSet},
          {"surface_materials", value.surfaceMaterials}};
}

TerrainPalette readPalette(const Json &value) {
  require(value.is_object(), "palette must be an object");
  TerrainPalette result;
  result.formatVersion = value.at("format_version").get<int>();
  require(result.formatVersion == TerrainPalette::CurrentFormatVersion,
          "palette format is unsupported");
  result.id = value.at("id").get<std::string>();
  result.name = value.at("name").get<std::string>();
  result.artDensity =
      finiteFloat(value.at("art_density"), "palette art density");
  result.grassDensity =
      finiteFloat(value.at("grass_density"), "palette grass density");
  result.treeDensity =
      finiteFloat(value.at("tree_density"), "palette tree density");
  result.rockDensity =
      finiteFloat(value.at("rock_density"), "palette rock density");
  require(result.artDensity >= 0 && result.grassDensity >= 0 &&
              result.treeDensity >= 0 && result.rockDensity >= 0,
          "palette density multipliers must be nonnegative");
  result.materialSet = value.at("material_set").get<std::string>();
  result.surfaceMaterials =
      value.at("surface_materials").get<decltype(result.surfaceMaterials)>();
  const auto &rules = value.at("placements");
  require(rules.is_object(), "palette placements must be an object");
  for (const auto &[name, item] : rules.items()) {
    require(validTerrainPlacementRuleId(name),
            "palette has an invalid rule ID");
    TerrainPaletteEntry entry;
    entry.model = item.at("model").get<std::string>();
    entry.prefab = item.at("prefab").get<std::string>();
    require(entry.model.empty() != entry.prefab.empty(),
            "palette placement requires exactly one model or prefab");
    entry.density = finiteFloat(item.at("density"), "palette density");
    entry.candidateSpacing =
        finiteFloat(item.at("candidate_spacing"), "palette candidate spacing");
    entry.positionJitter =
        finiteFloat(item.at("position_jitter"), "palette position jitter");
    const auto category = item.at("category").get<std::string>();
    bool categoryFound = false;
    for (const auto candidate :
         {TerrainScatterCategory::Prop, TerrainScatterCategory::Grass,
          TerrainScatterCategory::Tree, TerrainScatterCategory::Rock}) {
      if (terrainScatterCategoryName(candidate) == category) {
        entry.category = candidate;
        categoryFound = true;
        break;
      }
    }
    require(categoryFound, "palette contains an invalid category");
    const auto range = [&](const char *field, float lower, float upper) {
      const auto &values = item.at(field);
      require(values.is_array() && values.size() == 2,
              "palette range needs two values");
      TerrainScatterRange parsed{finiteFloat(values[0], field),
                                 finiteFloat(values[1], field)};
      require(parsed.minimum >= lower && parsed.maximum <= upper &&
                  parsed.minimum <= parsed.maximum,
              "palette range is invalid");
      return parsed;
    };
    entry.slope = range("slope", 0, 90);
    entry.moisture = range("moisture", 0, 1);
    entry.waterDistance =
        range("water_distance", 0, std::numeric_limits<float>::max());
    entry.yawDegrees = range("yaw", -360, 360);
    entry.clusterSize =
        finiteFloat(item.at("cluster_size"), "palette cluster size");
    entry.clusterStrength =
        finiteFloat(item.at("cluster_strength"), "palette cluster strength");
    entry.scaleMin = finiteFloat(item.at("scale_min"), "palette minimum scale");
    entry.scaleMax = finiteFloat(item.at("scale_max"), "palette maximum scale");
    entry.spacing = finiteFloat(item.at("spacing"), "palette spacing");
    entry.collision = readCollision(item.at("collision"));
    require(entry.model.empty() ||
                entry.collision == TerrainCollisionPolicy::None,
            "palette direct model placements require collision none; use an "
            "authored prefab collider");
    entry.biomes = item.at("biomes").get<std::vector<std::string>>();
    require(entry.density >= 0 && entry.candidateSpacing >= 0 &&
                entry.positionJitter >= 0 && entry.positionJitter <= 1 &&
                entry.scaleMin > 0 && entry.scaleMin <= entry.scaleMax &&
                entry.spacing >= 0 && entry.clusterSize > 0 &&
                entry.clusterStrength >= 0 && entry.clusterStrength <= 1,
            "palette contains invalid placement dimensions");
    const double step = entry.candidateStep();
    const double probability =
        double(entry.density) * result.densityFor(entry.category) * step * step;
    require(std::isfinite(probability) && probability <= 1.000001,
            "palette density exceeds candidate capacity");
    result.placements.emplace(name, std::move(entry));
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
  validateTerrainWaterFlow(value);
  require(std::isfinite(value.riverDepth) && value.riverDepth > 0 &&
              (value.kind == TerrainWaterBody::River ||
               value.riverSurfaceMode == TerrainRiverSurfaceMode::Level),
          "river surface mode/depth do not match the water body");
  Json boundary = Json::array();
  for (const auto &point : value.boundary) {
    boundary.push_back(vec2(point));
  }
  Json path = Json::array();
  for (const auto &point : value.riverPath)
    path.push_back(vec3(point));
  return {{"id", value.id},
          {"kind", std::string(waterKind(value.kind))},
          {"level", value.level},
          {"river_surface_mode",
           std::string(terrainRiverSurfaceModeName(value.riverSurfaceMode))},
          {"river_depth", value.riverDepth},
          {"flow_speed", value.flowSpeed},
          {"flow_direction", vec2(value.flowDirection)},
          {"river_path", std::move(path)},
          {"river_width", value.riverWidth},
          {"center", vec2(value.center)},
          {"radius", value.radius},
          {"boundary", std::move(boundary)},
          {"shoreline_softening", value.shorelineSoftening},
          {"appearance", terrainWaterAppearanceJson(value.appearance)}};
}

TerrainWaterBodySpec readWaterBody(const Json &value) {
  TerrainWaterBodySpec result;
  result.id = value.at("id").get<std::string>();
  require(!result.id.empty(), "water body id must not be empty");
  result.kind = readWaterKind(value.at("kind"));
  result.level = finiteFloat(value.at("level"), "water level");
  result.riverSurfaceMode = parseTerrainRiverSurfaceMode(
      value.at("river_surface_mode").get<std::string>());
  result.riverDepth = finiteFloat(value.at("river_depth"), "river depth");
  result.flowSpeed = finiteFloat(value.at("flow_speed"), "flow speed");
  result.flowDirection = readVec2(value.at("flow_direction"), "flow direction");
  validateTerrainWaterFlow(result);
  require(result.riverDepth > 0 &&
              (result.kind == TerrainWaterBody::River ||
               result.riverSurfaceMode == TerrainRiverSurfaceMode::Level),
          "river surface mode/depth do not match the water body");
  const auto &path = value.at("river_path");
  require(path.is_array(), "river path must be an array");
  for (const auto &point : path)
    result.riverPath.push_back(readVec3(point, "river path point"));
  result.riverWidth = finiteFloat(value.at("river_width"), "river width");
  result.center = readVec2(value.at("center"), "water center");
  result.radius = finiteFloat(value.at("radius"), "water radius");
  const auto &boundary = value.at("boundary");
  require(boundary.is_array(), "water boundary must be an array");
  for (const auto &point : boundary) {
    result.boundary.push_back(readVec2(point, "water boundary point"));
  }
  result.shorelineSoftening = value.at("shoreline_softening").get<bool>();
  result.appearance = parseTerrainWaterAppearance(value.at("appearance"));
  require(result.riverWidth >= 0 && result.radius >= 0,
          "water body dimensions must be nonnegative");
  // Authored data retains bodies dropped during generation, including rivers
  // with incomplete paths. Preserve those IDs and their coverage indices;
  // acceptedBodies decides which geometry is usable after decoding.
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
              coverage.body.size() == count &&
              coverage.bodyLevels.size() == authoring.bodies.size(),
          "water coverage arrays must match the terrain grid");
  std::vector<std::uint8_t> accepted(authoring.bodies.size());
  for (const auto body : terrain_water_detail::acceptedBodies(authoring)) {
    accepted[body] = 1;
    const auto &spec = authoring.bodies[body];
    const auto &levels = coverage.bodyLevels[body];
    const bool varying =
        spec.riverSurfaceMode == TerrainRiverSurfaceMode::FollowBed;
    require(bool(levels.samples) == varying,
            "water body level representation must match its surface mode");
    if (!varying) {
      require(levels.constant == spec.level,
              "constant water profile must match its authored level");
      continue;
    }
    require(levels.samples->size() == count,
            "water body level grid must match the terrain grid");
    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    for (const auto &point : spec.riverPath) {
      minimum = std::min(minimum, point.y + spec.riverDepth);
      maximum = std::max(maximum, point.y + spec.riverDepth);
    }
    for (const float level : *levels.samples) {
      require(std::isfinite(level) && level >= minimum && level <= maximum,
              "river level grid must stay within its authored profile bounds");
    }
  }
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
    require(coverage.level[sample] == coverage.bodyLevels[body].at(sample),
            "water coverage level must match its owning body profile");
  }
}

Json waterCoverage(const TerrainWaterResult &value, const HeightField &field,
                   const TerrainWaterAuthoring &authoring) {
  if (!value.resolvedCoverage)
    return nullptr;
  const auto &coverage = *value.resolvedCoverage;
  validateWaterCoverage(coverage, field, authoring);
  Json bodies = Json::array();
  Json profiles = Json::array();
  for (const auto &levels : coverage.bodyLevels) {
    profiles.push_back(levels.samples ? Json(*levels.samples) : Json(nullptr));
  }
  for (const auto body : coverage.body) {
    if (body == terrain_water_detail::WaterLevelField::noBody)
      bodies.push_back(-1);
    else
      bodies.push_back(body);
  }
  // Grid dimensions come from the terrain; flags and levels come from owners.
  // Ownership includes dry shore samples and must survive without re-solving.
  return {{"body", std::move(bodies)}, {"body_levels", std::move(profiles)}};
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
  const auto &profiles = value.at("body_levels");
  require(profiles.is_array() && profiles.size() == authoring.bodies.size(),
          "water level profiles must match authored bodies");
  coverage->bodyLevels.resize(profiles.size());
  for (std::size_t body = 0; body < profiles.size(); ++body) {
    auto &levels = coverage->bodyLevels[body];
    levels.constant = authoring.bodies[body].level;
    if (profiles[body].is_null()) {
      continue;
    }
    require(profiles[body].is_array() && profiles[body].size() == count,
            "water body level grid must match the terrain grid");
    auto samples = std::make_shared<std::vector<float>>();
    samples->reserve(count);
    for (const auto &level : profiles[body]) {
      samples->push_back(finiteFloat(level, "river surface level"));
    }
    levels.samples = std::move(samples);
  }
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
    coverage->level[sample] = coverage->bodyLevels[body].at(sample);
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
    surfaces.push_back(
        {{"id", surface.id},
         {"kind", std::string(waterKind(surface.kind))},
         {"level", surface.level},
         {"river_surface_mode",
          std::string(terrainRiverSurfaceModeName(surface.riverSurfaceMode))},
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
    surface.riverSurfaceMode = parseTerrainRiverSurfaceMode(
        entry.at("river_surface_mode").get<std::string>());
    const auto body = std::ranges::find(authoring.bodies, surface.id,
                                        &TerrainWaterBodySpec::id);
    require(body != authoring.bodies.end() && body->kind == surface.kind &&
                body->riverSurfaceMode == surface.riverSurfaceMode &&
                (surface.riverSurfaceMode != TerrainRiverSurfaceMode::Level ||
                 body->level == surface.level),
            "water surface metadata must match its authored body");
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

Json encodeSurfaceMaterials(const HeightField &field) {
  if (!field.surfaceMaterials) {
    return nullptr;
  }

  const auto &classification = *field.surfaceMaterials->classification;
  const std::vector<std::uint8_t> samples(classification.samples.begin(),
                                          classification.samples.end());
  return {{"roles", classification.roles}, {"samples", samples}};
}

std::shared_ptr<const TerrainSurfaceMaterials>
decodeSurfaceMaterials(const Json &stored, const HeightField &field) {
  if (stored.is_null()) {
    return {};
  }
  require(field.resolvedPalette && !field.resolvedPalette->materialSet.empty(),
          "surface materials require a resolved palette material set");

  auto classification = std::make_shared<TerrainSurfaceClassification>();
  classification->roles = stored.at("roles").get<std::vector<std::string>>();
  const auto roleCount = classification->roles.size();
  require(roleCount > 0 && roleCount <= 256,
          "surface material role table has invalid entry count: " +
              std::to_string(roleCount));
  std::set<std::string> uniqueRoles;
  for (const auto &role : classification->roles) {
    // An empty role is the scorer's explicit unassigned result.
    require(role.empty() || terrainMaterialRoleFromName(role).has_value(),
            "unknown surface material role");
    require(uniqueRoles.insert(role).second,
            "surface material role table contains duplicates");
  }

  const auto &storedSamples = stored.at("samples");
  require(storedSamples.is_array() && storedSamples.size() == field.heights.size(),
          "surface material samples do not match the heightfield");
  classification->samples.resize(field.heights.size());
  for (std::size_t sample = 0; sample < field.heights.size(); ++sample) {
    const auto roleIndex = checkedSize(storedSamples[sample],
                                       "surface material role index");
    require(roleIndex < roleCount,
            "surface material role index is out of range");
    classification->samples.set(sample, static_cast<std::uint8_t>(roleIndex));
  }

  auto surface = std::make_shared<TerrainSurfaceMaterials>();
  surface->materialSet = field.resolvedPalette->materialSet;
  surface->bindings = field.resolvedPalette->surfaceMaterials;
  surface->classification = std::move(classification);
  for (const auto &role : surface->classification->roles) {
    if (!surface->bindings.contains(role)) {
      surface->unboundRoles.push_back(role);
    }
  }
  return surface;
}

} // namespace

Json encodeDerived(const HeightField &field) {
  Json result{{"format_version", 1},
              {"scatter", placements(field.scatterPlacements)},
              {"scatter_truncated", field.scatterTruncated},
              {"palette", field.resolvedPalette
                              ? palette(*field.resolvedPalette)
                              : Json(nullptr)},
              {"surface_materials", encodeSurfaceMaterials(field)},
              {"graph", nullptr}};
  if (field.graphArtifacts) {
    const auto &artifacts = *field.graphArtifacts;
    Json runs = Json::array();
    for (const auto &run : artifacts.nodes) {
      Json encoded{{"id", run.id},
                   {"cached", run.cached},
                   {"milliseconds", run.milliseconds}};
      if (!run.status.empty()) {
        encoded["status"] = run.status;
      }
      runs.push_back(std::move(encoded));
    }
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
    if (!artifacts.hydraulicTransport.empty()) {
      Json transport = Json::object();
      for (const auto &[id, ledger] : artifacts.hydraulicTransport) {
        transport[id] = transportLedger(ledger);
      }
      result["graph"]["hydraulic_transport"] = std::move(transport);
    }
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
  field.surfaceMaterials = decodeSurfaceMaterials(
      document.at("surface_materials"), field);
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
      decoded.status = run.value("status", std::string{});
      const double elapsed = run.at("milliseconds").get<double>();
      require(std::isfinite(elapsed) && elapsed >= 0,
              "graph run time must be finite and nonnegative");
      decoded.milliseconds = elapsed;
      artifacts->nodes.push_back(std::move(decoded));
    }
    artifacts->warnings = graph.at("warnings").get<std::vector<std::string>>();
    if (graph.contains("hydraulic_transport")) {
      const auto &transport = graph.at("hydraulic_transport");
      require(transport.is_object(), "graph hydraulic transport must be an object");
      for (const auto &[id, ledger] : transport.items()) {
        const auto run = std::ranges::find(artifacts->nodes, id,
                                           &TerrainGraphNodeRun::id);
        require(run != artifacts->nodes.end(),
                "hydraulic transport references a missing graph node");
        artifacts->hydraulicTransport.emplace(id, readTransportLedger(ledger));
      }
    }
    field.graphArtifacts = std::move(artifacts);
  }
}

} // namespace demi::assets::terrain_payload
