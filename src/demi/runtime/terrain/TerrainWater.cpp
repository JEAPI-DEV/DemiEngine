#include "demi/runtime/terrain/TerrainWater.h"

#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainMasks.h"
#include "demi/runtime/terrain/TerrainWaterConnectivity.h"
#include "demi/runtime/terrain/TerrainWaterGeometry.h"
#include "demi/runtime/terrain/TerrainWaterSurfaceBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace demi::runtime {
namespace {

// A shoreline band two cells wide: wide enough to be visible at a coast, narrow
// enough that it does not rewrite the land behind the shore.
constexpr double shorelineBandCells = 2.0;
// A lake deepens its own fill by a quarter. Proportional, so a shallow pan and
// a deep basin are cut by the same ratio and the shoreline, where there is no
// fill to cut, never moves.
constexpr float lakeBasinCut = 0.25F;
// The most the band may cut at the waterline, as a fraction of the band width.
// Bounded so a cliff becomes a steep beach rather than a trench.
constexpr float shorelineCutFraction = 0.5F;
// Fraction of a river's length spent tapering in at each end.
constexpr double riverEndTaper = 0.2;
// A channel with no drainage data behind it still cuts, at half depth.
constexpr float channelFlowFloor = 0.5F;

double clamp01(double value) { return std::clamp(value, 0.0, 1.0); }
double smoothstep01(double value) {
  const auto t = clamp01(value);
  return t * t * (3.0 - 2.0 * t);
}
bool finitePoint(Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}
std::size_t sampleCount(int cellsX, int cellsZ) {
  return (std::size_t(cellsX) + 1) * (std::size_t(cellsZ) + 1);
}

// A polyline prepared once per body: a per-sample search that rebuilt arc
// lengths would be quadratic in the path length for no reason.
struct RiverPath {
  std::vector<double> start;
  std::vector<double> length;
  double total = 0;
};

RiverPath prepareRiverPath(const std::vector<Vec3> &points,
                           std::stop_token stop) {
  RiverPath path;
  path.start.resize(points.size());
  path.length.assign(points.size() > 1 ? points.size() - 1 : 0, 0.0);
  for (std::size_t i = 0; i < points.size(); ++i) {
    if ((i & 255) == 0 && stop.stop_requested())
      return path;
    path.start[i] = path.total;
    if (i + 1 < points.size()) {
      const double dx = double(points[i + 1].x) - double(points[i].x);
      const double dz = double(points[i + 1].z) - double(points[i].z);
      path.length[i] = std::hypot(dx, dz);
      path.total += path.length[i];
    }
  }
  return path;
}

struct RiverSample {
  double distance = std::numeric_limits<double>::infinity();
  // 0..1 over the whole path, which is what the end taper reads.
  double along = 0;
  double bedHeight = 0;
};

RiverSample nearestOnPath(const std::vector<Vec3> &points,
                          const RiverPath &path, double x, double z,
                          std::stop_token stop) {
  RiverSample best;
  if (path.total <= 0)
    return best;
  for (std::size_t i = 0; i < path.length.size(); ++i) {
    if ((i & 255) == 0 && stop.stop_requested())
      return best;
    const double ax = double(points[i].x);
    const double az = double(points[i].z);
    const double bx = double(points[i + 1].x);
    const double bz = double(points[i + 1].z);
    const double dx = bx - ax;
    const double dz = bz - az;
    const double span = path.length[i];
    // Clamped projection: a sample beyond an end attaches to that end, which is
    // what makes the channel stop at its own ends instead of continuing.
    const double t = clamp01(((x - ax) * dx + (z - az) * dz) / (span * span));
    const double distance = std::hypot(x - (ax + dx * t), z - (az + dz * t));
    if (distance >= best.distance)
      continue;
    best.distance = distance;
    best.along = (path.start[i] + span * t) / path.total;
    best.bedHeight = double(points[i].y) +
                     (double(points[i + 1].y) - double(points[i].y)) * t;
  }
  return best;
}

// One body, with the per-body work done once: the coverage predicate is asked
// per sample, so the polyline cannot be rebuilt per sample.
struct BodyProbe {
  const TerrainWaterBodySpec *spec = nullptr;
  RiverPath river;
  std::stop_token stop;
};

BodyProbe probeFor(const TerrainWaterBodySpec &spec,
                   std::stop_token stop = {}) {
  BodyProbe probe;
  probe.spec = &spec;
  probe.stop = stop;
  if (spec.kind == TerrainWaterBody::River)
    probe.river = prepareRiverPath(spec.riverPath, stop);
  return probe;
}

// The authored width, tapering to nothing at both ends so a river opens and
// closes instead of starting and stopping with a cut face.
double channelHalfWidth(double width, double along) {
  const double taper = smoothstep01(along / riverEndTaper) *
                       smoothstep01((1.0 - along) / riverEndTaper);
  return 0.5 * width * taper;
}

// Does water exist at this sample? Two conditions, and both matter: the body
// must reach the sample, and the ground must be at or below the water plane.
// A body drawn on ground it sits above is not water.
bool covers(const BodyProbe &probe, double x, double z, double ground) {
  const auto &spec = *probe.spec;
  if (ground > double(spec.level))
    return false;
  if (spec.kind == TerrainWaterBody::River) {
    const auto nearest =
        nearestOnPath(spec.riverPath, probe.river, x, z, probe.stop);
    if (!std::isfinite(nearest.distance))
      return false;
    return nearest.distance <=
           channelHalfWidth(double(spec.riverWidth), nearest.along);
  }
  if (double(spec.radius) > 0) {
    const double dx = x - double(spec.center.x);
    const double dz = z - double(spec.center.y);
    return dx * dx + dz * dz <= double(spec.radius) * double(spec.radius);
  }
  return true;
}

// Which samples a body covers, plus how far every sample is from that water.
// The distance is a two-pass chamfer: exact enough to place a shoreline band,
// and it cannot disagree with the coverage mask because it is derived from it.
struct BodyCoverage {
  std::vector<std::uint8_t> covered;
  std::vector<float> distance;
};

BodyCoverage coverBody(const BodyProbe &probe, Vec2 size, int cellsX,
                       int cellsZ, const std::vector<float> &ground,
                       std::stop_token stop) {
  const auto columns = std::size_t(cellsX) + 1;
  const auto count = columns * (std::size_t(cellsZ) + 1);
  BodyCoverage coverage;
  coverage.covered.assign(count, 0);
  coverage.distance.assign(count, std::numeric_limits<float>::infinity());
  const auto at = [&](int x, int z) {
    return std::size_t(z) * columns + std::size_t(x);
  };
  for (int z = 0; z <= cellsZ; ++z) {
    if (stop.stop_requested())
      return coverage;
    for (int x = 0; x <= cellsX; ++x) {
      const auto sample = at(x, z);
      const auto position = Vec2{float(double(x) * size.x / cellsX),
                                 float(double(z) * size.y / cellsZ)};
      if (!covers(probe, double(position.x), double(position.y),
                  double(ground[sample])))
        continue;
      coverage.covered[sample] = 1;
    }
  }
  if (probe.spec->kind == TerrainWaterBody::Lake) {
    for (std::size_t sample = 0; sample < count; ++sample)
      if (!(ground[sample] < probe.spec->level))
        coverage.covered[sample] = 0;
    auto connected = terrain_water_detail::connectedLakeCoverage(
        size, cellsX, cellsZ, probe.spec->center, coverage.covered, stop);
    if (stop.stop_requested())
      return coverage;
    coverage.covered = std::move(connected);
    // Exact waterline samples can start the shoreline band, but must not
    // connect two basins through zero-depth land during the flood fill.
    const auto selected = coverage.covered;
    constexpr std::array<std::array<int, 2>, 6> edges{
        {{{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}}, {{-1, 1}}, {{1, -1}}}};
    for (int z = 0; z <= cellsZ; ++z) {
      if (stop.stop_requested())
        return coverage;
      for (int x = 0; x <= cellsX; ++x) {
        const auto sample = at(x, z);
        if (ground[sample] != probe.spec->level)
          continue;
        const auto position = Vec2{float(double(x) * size.x / cellsX),
                                   float(double(z) * size.y / cellsZ)};
        if (!covers(probe, position.x, position.y, ground[sample]))
          continue;
        for (const auto &edge : edges) {
          const int nx = x + edge[0], nz = z + edge[1];
          if (nx >= 0 && nz >= 0 && nx <= cellsX && nz <= cellsZ &&
              selected[at(nx, nz)]) {
            coverage.covered[sample] = 1;
            break;
          }
        }
      }
    }
  }
  for (std::size_t sample = 0; sample < count; ++sample)
    if (coverage.covered[sample])
      coverage.distance[sample] = 0.F;

  const double stepX = double(size.x) / cellsX;
  const double stepZ = double(size.y) / cellsZ;
  const double diagonal = std::hypot(stepX, stepZ);
  const auto relax = [&](std::size_t sample, std::size_t neighbour,
                         double step) {
    const auto candidate = coverage.distance[neighbour] + float(step);
    if (candidate < coverage.distance[sample])
      coverage.distance[sample] = candidate;
  };
  for (int z = 0; z <= cellsZ; ++z) {
    if (stop.stop_requested())
      return coverage;
    for (int x = 0; x <= cellsX; ++x) {
      const auto sample = at(x, z);
      if (x > 0)
        relax(sample, sample - 1, stepX);
      if (z > 0)
        relax(sample, sample - columns, stepZ);
      if (x > 0 && z > 0)
        relax(sample, sample - columns - 1, diagonal);
      if (x < cellsX && z > 0)
        relax(sample, sample - columns + 1, diagonal);
    }
  }
  for (int z = cellsZ; z >= 0; --z) {
    if (stop.stop_requested())
      return coverage;
    for (int x = cellsX; x >= 0; --x) {
      const auto sample = at(x, z);
      if (x < cellsX)
        relax(sample, sample + 1, stepX);
      if (z < cellsZ)
        relax(sample, sample + columns, stepZ);
      if (x < cellsX && z < cellsZ)
        relax(sample, sample + columns + 1, diagonal);
      if (x > 0 && z < cellsZ)
        relax(sample, sample + columns - 1, diagonal);
    }
  }
  return coverage;
}

// The drainage mask modulates how hard a river cuts: water concentrates where
// the landform already drains there, so a channel that ignores drainage would
// contradict the masks a biome rule is reading at the same sample.
double channelGain(const TerrainMasks *masks, std::size_t sample) {
  if (masks == nullptr)
    return 1.0;
  const float flow = std::clamp(masks->flow[sample], 0.F, 1.F);
  return double(channelFlowFloor) +
         double(1.F - channelFlowFloor) * double(flow);
}

void carveRiver(const TerrainWaterBodySpec &spec, const BodyProbe &probe,
                Vec2 size, int cellsX, int cellsZ, const TerrainMasks *masks,
                std::vector<float> &carved, const std::vector<float> &ground,
                std::stop_token stop) {
  if (probe.river.total <= 0 || !(spec.riverWidth > 0))
    return;
  const auto columns = std::size_t(cellsX) + 1;
  for (int z = 0; z <= cellsZ; ++z) {
    if (stop.stop_requested())
      return;
    for (int x = 0; x <= cellsX; ++x) {
      const auto sample = std::size_t(z) * columns + std::size_t(x);
      // The channel is cut along the authored path whether or not the water
      // reaches this sample: the path is the author's intent, and a river that
      // stopped cutting wherever the ground rose above its level would be a
      // channel that begins and ends for no reason.
      const auto position = Vec2{float(double(x) * size.x / cellsX),
                                 float(double(z) * size.y / cellsZ)};
      const auto nearest =
          nearestOnPath(spec.riverPath, probe.river, double(position.x),
                        double(position.y), stop);
      if (!std::isfinite(nearest.distance))
        continue;
      const double halfWidth =
          channelHalfWidth(double(spec.riverWidth), nearest.along);
      if (halfWidth <= 0 || nearest.distance >= halfWidth)
        continue;
      const double across = 1.0 - smoothstep01(nearest.distance / halfWidth);
      const double gain = across * channelGain(masks, sample);
      const double target =
          ground[sample] + (nearest.bedHeight - double(ground[sample])) * gain;
      carved[sample] = float(std::min(double(carved[sample]), target));
    }
  }
}

void carveLake(const TerrainWaterBodySpec &spec, const BodyCoverage &coverage,
               const std::vector<float> &ground, std::vector<float> &carved,
               std::stop_token stop) {
  for (std::size_t sample = 0; sample < carved.size(); ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested())
      return;
    if (!coverage.covered[sample])
      continue;
    const double fill = double(spec.level) - double(ground[sample]);
    if (fill <= 0)
      continue;
    carved[sample] =
        float(double(ground[sample]) - double(lakeBasinCut) * fill);
  }
}

// The shore, not the basin: a body with no carve of its own still gets a
// beach, because an unsmoothed waterline is a cliff and a cliff is what makes
// procedural shorelines read as procedural.
void carveShoreline(const TerrainWaterBodySpec &spec, Vec2 size, int cellsX,
                    int cellsZ, const BodyCoverage &coverage,
                    std::vector<float> &carved, std::stop_token stop) {
  if (!spec.shorelineSoftening)
    return;
  const double band = shorelineBandCells * std::max(double(size.x) / cellsX,
                                                    double(size.y) / cellsZ);
  if (!(band > 0))
    return;
  const double reach = band * double(shorelineCutFraction);
  for (std::size_t sample = 0; sample < carved.size(); ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested())
      return;
    if (coverage.covered[sample])
      continue;
    // Only ground that stands above the water is shore. A dry sample whose
    // ground already sits below the level is dry because the body does not
    // reach it, and lowering it would fill in land the author left dry.
    const double rise = double(carved[sample]) - double(spec.level);
    if (!(rise > 0))
      continue;
    const double distance = double(coverage.distance[sample]);
    if (!std::isfinite(distance) || distance >= band)
      continue;
    const double weight = 1.0 - distance / band;
    // Bounded by the remaining relief: a shore that is already at the water
    // plane is not cut further, which keeps the band from moving the shoreline.
    carved[sample] =
        float(double(carved[sample]) - weight * std::min(rise, reach));
  }
}

terrain_water_detail::WaterLevelField
buildField(Vec2 size, int cellsX, int cellsZ, const std::vector<float> &ground,
           const TerrainWaterAuthoring &authoring, std::stop_token stop = {},
           bool includeDryFootprint = false) {
  using terrain_water_detail::WaterLevelField;
  WaterLevelField field;
  field.size = size;
  field.cellsX = cellsX;
  field.cellsZ = cellsZ;
  const auto count = sampleCount(cellsX, cellsZ);
  field.level.assign(count, 0.F);
  field.wet.assign(count, 0);
  field.body.assign(count, WaterLevelField::noBody);
  for (const auto index :
       terrain_water_detail::acceptedBodies(authoring, stop)) {
    if (stop.stop_requested())
      return field;
    const auto &spec = authoring.bodies[index];
    const auto probe = probeFor(spec, stop);
    std::vector<std::uint8_t> connected;
    if (spec.kind == TerrainWaterBody::Lake) {
      std::vector<std::uint8_t> wet(count);
      for (int z = 0; z <= cellsZ; ++z) {
        if (stop.stop_requested())
          return field;
        for (int x = 0; x <= cellsX; ++x) {
          const auto sample = field.sampleAt(x, z);
          const auto position = field.position(x, z);
          wet[sample] = std::isfinite(ground[sample]) &&
                        ground[sample] < spec.level &&
                        covers(probe, position.x, position.y, ground[sample]);
        }
      }
      connected = terrain_water_detail::connectedLakeCoverage(
          size, cellsX, cellsZ, spec.center, wet, stop);
      if (stop.stop_requested())
        return field;
    }
    for (int z = 0; z <= cellsZ; ++z) {
      if (stop.stop_requested())
        return field;
      for (int x = 0; x <= cellsX; ++x) {
        if ((x & 255) == 0 && stop.stop_requested())
          return field;
        const auto sample = field.sampleAt(x, z);
        const auto position = field.position(x, z);
        if (!std::isfinite(ground[sample]))
          continue;
        const double coverageGround =
            includeDryFootprint ? -std::numeric_limits<double>::infinity()
                                : double(ground[sample]);
        if (!covers(probe, double(position.x), double(position.y),
                    coverageGround))
          continue;
        if (spec.kind == TerrainWaterBody::Lake && !connected[sample]) {
          if (!includeDryFootprint || ground[sample] < spec.level)
            continue;
          // Only a one-edge dry halo participates in shoreline interpolation.
          // Extending the whole footprint would recreate disconnected puddles.
          bool shoreline = false;
          constexpr std::array<std::array<int, 2>, 6> edges{
              {{{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}}, {{-1, 1}}, {{1, -1}}}};
          for (const auto &edge : edges) {
            const int nx = x + edge[0], nz = z + edge[1];
            if (nx >= 0 && nz >= 0 && nx <= cellsX && nz <= cellsZ &&
                connected[field.sampleAt(nx, nz)]) {
              shoreline = true;
              break;
            }
          }
          if (!shoreline)
            continue;
        }
        // A dry footprint supplies shoreline interpolation, but cannot steal
        // water from an earlier body whose level still covers this sample.
        if (includeDryFootprint && ground[sample] >= spec.level &&
            field.wet[sample] && ground[sample] <= field.level[sample])
          continue;
        // Authored order is the priority order: a later body overwrites the
        // overlap rather than blending with it, so the result is decidable
        // without asking which body "really" owns a shared cell.
        field.level[sample] = spec.level;
        field.wet[sample] = 1;
        field.body[sample] = index;
      }
    }
  }
  return field;
}

std::optional<TerrainWaterResult> finishWaterResult(
    Vec2 size, int cellsX, int cellsZ, const TerrainWaterAuthoring &authoring,
    const std::vector<std::size_t> &accepted, const std::vector<float> &carved,
    TerrainSamples<float> carvedHeights, std::stop_token stop) {
  // Coverage, ownership and depth all read the finished ground. Later bodies
  // own overlaps, even when an earlier body carved part of the same basin.
  auto resolved =
      buildField(size, cellsX, cellsZ, carved, authoring, stop, true);
  if (stop.stop_requested())
    return std::nullopt;
  TerrainWaterResult result;
  result.dropped = authoring.bodies.size() - accepted.size();
  result.carvedHeights = std::move(carvedHeights);
  result.resolvedCoverage =
      std::make_shared<const terrain_water_detail::WaterLevelField>(
          std::move(resolved));
  result.surfaces.reserve(accepted.size());
  for (const auto index : accepted) {
    if (stop.stop_requested())
      return std::nullopt;
    result.surfaces.push_back(terrain_water_detail::buildTerrainWaterSurface(
        authoring.bodies[index], index, *result.resolvedCoverage, carved,
        stop));
  }
  if (stop.stop_requested())
    return std::nullopt;
  return result;
}
} // namespace

namespace terrain_water_detail {

std::vector<std::size_t> acceptedBodies(const TerrainWaterAuthoring &authoring,
                                        std::stop_token stop) {
  std::vector<std::size_t> accepted;
  accepted.reserve(authoring.bodies.size());
  std::unordered_set<std::string> seenIds;
  for (std::size_t index = 0; index < authoring.bodies.size(); ++index) {
    if (stop.stop_requested())
      return accepted;
    const auto &spec = authoring.bodies[index];
    if (spec.id.empty() || !seenIds.insert(spec.id).second)
      continue;
    if (!std::isfinite(spec.level) || !std::isfinite(spec.radius) ||
        !std::isfinite(spec.center.x) || !std::isfinite(spec.center.y))
      continue;
    try {
      validateTerrainWaterAppearance(spec.appearance);
    } catch (const std::invalid_argument &) {
      continue;
    }
    if (spec.kind == TerrainWaterBody::River) {
      if (spec.riverPath.size() < 2 || !std::isfinite(spec.riverWidth) ||
          !(spec.riverWidth > 0))
        continue;
      bool finite = true;
      for (const auto &point : spec.riverPath) {
        if (stop.stop_requested())
          return accepted;
        finite = finite && finitePoint(point);
      }
      if (!finite)
        continue;
    }
    accepted.push_back(index);
  }
  return accepted;
}

WaterLevelField buildWaterLevelField(Vec2 size, int cellsX, int cellsZ,
                                     const TerrainSamples<float> &ground,
                                     const TerrainWaterAuthoring &authoring) {
  const auto count = sampleCount(cellsX, cellsZ);
  if (ground.size() != count)
    return {};
  std::vector<float> samples(count);
  for (std::size_t sample = 0; sample < count; ++sample)
    samples[sample] = ground[sample];
  return buildField(size, cellsX, cellsZ, samples, authoring);
}

WaterLevelField buildWaterGeometryField(Vec2 size, int cellsX, int cellsZ,
                                        const TerrainSamples<float> &ground,
                                        const TerrainWaterAuthoring &authoring,
                                        std::stop_token stop) {
  if (cellsX <= 0 || cellsZ <= 0 ||
      ground.size() != sampleCount(cellsX, cellsZ))
    return {};
  std::vector<float> samples(ground.size());
  for (std::size_t sample = 0; sample < samples.size(); ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested())
      return {};
    samples[sample] = ground[sample];
  }
  auto field = buildField(size, cellsX, cellsZ, samples, authoring, stop, true);
  return stop.stop_requested() ? WaterLevelField{} : std::move(field);
}
} // namespace terrain_water_detail

std::optional<TerrainWaterResult>
carveTerrainWater(const HeightField &field,
                  const TerrainWaterAuthoring &authoring,
                  const TerrainMasks *masks, std::stop_token stop) {
  if (stop.stop_requested())
    return std::nullopt;
  if (!authoring.authored || !std::isfinite(authoring.seaLevel))
    return std::nullopt;
  if (field.cellsX <= 0 || field.cellsZ <= 0 || !std::isfinite(field.size.x) ||
      !std::isfinite(field.size.y) || !(field.size.x > 0) ||
      !(field.size.y > 0))
    return std::nullopt;
  const auto count = sampleCount(field.cellsX, field.cellsZ);
  if (field.heights.size() != count)
    return std::nullopt;
  if (masks != nullptr && masks->flow.size() != count)
    return std::nullopt;

  const auto accepted = terrain_water_detail::acceptedBodies(authoring, stop);
  if (stop.stop_requested())
    return std::nullopt;
  std::vector<float> ground(count);
  for (std::size_t sample = 0; sample < count; ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    ground[sample] = field.heights[sample];
  }
  // The field is copied before anything is written, and the copy is assembled
  // into fresh pages at the end: TerrainSamples is copy-on-write, so a set() on
  // the result could never reach the field's pages, and doing the arithmetic on
  // a plain buffer keeps the carve off the paging path entirely.
  std::vector<float> carved = ground;

  for (const auto index : accepted) {
    if (stop.stop_requested())
      return std::nullopt;
    const auto &spec = authoring.bodies[index];
    const auto probe = probeFor(spec, stop);
    const auto coverage =
        coverBody(probe, field.size, field.cellsX, field.cellsZ, ground, stop);
    if (stop.stop_requested())
      return std::nullopt;
    if (spec.kind == TerrainWaterBody::River)
      carveRiver(spec, probe, field.size, field.cellsX, field.cellsZ, masks,
                 carved, ground, stop);
    else if (spec.kind == TerrainWaterBody::Lake)
      carveLake(spec, coverage, ground, carved, stop);
    if (stop.stop_requested())
      return std::nullopt;
    carveShoreline(spec, field.size, field.cellsX, field.cellsZ, coverage,
                   carved, stop);
  }
  if (stop.stop_requested())
    return std::nullopt;

  return finishWaterResult(field.size, field.cellsX, field.cellsZ, authoring,
                           accepted, carved, TerrainSamples<float>(carved),
                           stop);
}

std::optional<TerrainWaterResult> refreshTerrainWaterResult(
    const HeightField &grid, const TerrainWaterAuthoring &combinedAuthoring,
    const TerrainSamples<float> &alreadyCarvedHeights, std::stop_token stop) {
  if (stop.stop_requested() || !combinedAuthoring.authored ||
      !std::isfinite(combinedAuthoring.seaLevel))
    return std::nullopt;
  if (grid.cellsX <= 0 || grid.cellsZ <= 0 || !std::isfinite(grid.size.x) ||
      !std::isfinite(grid.size.y) || !(grid.size.x > 0) || !(grid.size.y > 0) ||
      alreadyCarvedHeights.size() != sampleCount(grid.cellsX, grid.cellsZ))
    return std::nullopt;

  const auto accepted =
      terrain_water_detail::acceptedBodies(combinedAuthoring, stop);
  if (stop.stop_requested())
    return std::nullopt;
  std::vector<float> ground(alreadyCarvedHeights.size());
  for (std::size_t sample = 0; sample < ground.size(); ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested())
      return std::nullopt;
    ground[sample] = alreadyCarvedHeights[sample];
  }
  return finishWaterResult(grid.size, grid.cellsX, grid.cellsZ,
                           combinedAuthoring, accepted, ground,
                           alreadyCarvedHeights, stop);
}
} // namespace demi::runtime
