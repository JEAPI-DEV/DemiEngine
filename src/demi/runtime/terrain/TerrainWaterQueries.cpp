#include "demi/runtime/terrain/TerrainWaterQueries.h"

#include "demi/runtime/terrain/TerrainMasks.h"
#include "demi/runtime/terrain/TerrainWaterGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace demi::runtime {
namespace {

std::size_t sampleCount(int cellsX, int cellsZ) {
  return (std::size_t(cellsX) + 1) * (std::size_t(cellsZ) + 1);
}

} // namespace

std::optional<TerrainWaterQueryContext> TerrainWaterQueryContext::build(
    const TerrainWaterAuthoring &authoring, const TerrainWaterResult &result,
    const TerrainMasks *masks, int cellsX, int cellsZ, Vec2 size) {
  if (!authoring.authored || cellsX <= 0 || cellsZ <= 0) {
    return std::nullopt;
  }
  if (!std::isfinite(size.x) || !std::isfinite(size.y) || !(size.x > 0) ||
      !(size.y > 0)) {
    return std::nullopt;
  }
  const auto count = sampleCount(cellsX, cellsZ);
  if (result.carvedHeights.size() != count) {
    return std::nullopt;
  }
  // The same rule the carve uses: a drainage field on a different grid is a
  // caller error, not something to approximate.
  if (masks != nullptr && masks->flow.size() != count) {
    return std::nullopt;
  }
  auto coverage = result.resolvedCoverage;
  if (!coverage)
    coverage = std::make_shared<const terrain_water_detail::WaterLevelField>(
        terrain_water_detail::buildWaterGeometryField(
            size, cellsX, cellsZ, result.carvedHeights, authoring));
  const auto &field = *coverage;
  if (field.samples() != count || field.wet.size() != count ||
      field.body.size() != count || field.cellsX != cellsX ||
      field.cellsZ != cellsZ || field.size.x != size.x ||
      field.size.y != size.y) {
    return std::nullopt;
  }
  for (std::size_t sample = 0; sample < count; ++sample)
    if (field.wet[sample] &&
        (field.body[sample] >= authoring.bodies.size() ||
         field.level[sample] != authoring.bodies[field.body[sample]].level))
      return std::nullopt;

  TerrainWaterQueryContext context;
  context.size_ = size;
  context.cellsX_ = cellsX;
  context.cellsZ_ = cellsZ;
  context.ground_ = result.carvedHeights;
  context.coverage_ = std::move(coverage);
  if (masks)
    context.flow_ = masks->flow;
  context.ids_.reserve(authoring.bodies.size());
  for (const auto &body : authoring.bodies) {
    context.ids_.push_back(body.id);
    context.kinds_.push_back(body.kind);
  }
  return context;
}

std::optional<TerrainWaterQuery>
TerrainWaterQueryContext::sample(Vec3 localPosition) const {
  if (!coverage_ || !std::isfinite(localPosition.x) ||
      !std::isfinite(localPosition.y) || !std::isfinite(localPosition.z)) {
    return std::nullopt;
  }
  const auto &level_ = coverage_->level;
  const auto &wet_ = coverage_->wet;
  const auto &body_ = coverage_->body;
  if (localPosition.x < 0 || localPosition.z < 0 || localPosition.x > size_.x ||
      localPosition.z > size_.y) {
    return std::nullopt;
  }
  auto cellX =
      std::clamp(int(std::floor(double(localPosition.x) / size_.x * cellsX_)),
                 0, cellsX_ - 1);
  auto cellZ =
      std::clamp(int(std::floor(double(localPosition.z) / size_.y * cellsZ_)),
                 0, cellsZ_ - 1);
  // The same corner positions the field itself is built from, so a query lands
  // on the same cell the carve did instead of a float-shifted neighbour.
  const auto cornerX = [&](int x) {
    return float(double(x) * size_.x / cellsX_);
  };
  const auto cornerZ = [&](int z) {
    return float(double(z) * size_.y / cellsZ_);
  };
  while (cellX > 0 && localPosition.x < cornerX(cellX))
    --cellX;
  while (cellX < cellsX_ - 1 && localPosition.x >= cornerX(cellX + 1))
    ++cellX;
  while (cellZ > 0 && localPosition.z < cornerZ(cellZ))
    --cellZ;
  while (cellZ < cellsZ_ - 1 && localPosition.z >= cornerZ(cellZ + 1))
    ++cellZ;
  const auto u = (double(localPosition.x) - cornerX(cellX)) /
                 (double(cornerX(cellX + 1)) - double(cornerX(cellX)));
  const auto v = (double(localPosition.z) - cornerZ(cellZ)) /
                 (double(cornerZ(cellZ + 1)) - double(cornerZ(cellZ)));

  const auto columns = std::size_t(cellsX_) + 1;
  const std::array<std::size_t, 4> corners{
      std::size_t(cellZ) * columns + std::size_t(cellX),
      std::size_t(cellZ) * columns + std::size_t(cellX + 1),
      std::size_t(cellZ + 1) * columns + std::size_t(cellX),
      std::size_t(cellZ + 1) * columns + std::size_t(cellX + 1)};
  const auto weights = terrain_water_detail::waterTriangleWeights(u, v);

  double ground = 0.0;
  double level = 0.0;
  double flow = 0.0;
  auto owner = terrain_water_detail::WaterLevelField::noBody;
  for (std::size_t corner = 0; corner < corners.size(); ++corner) {
    const auto sample = corners[corner];
    if (weights[corner] == 0.0)
      continue;
    if (!std::isfinite(ground_[sample]))
      return TerrainWaterQuery{};
    ground += weights[corner] * double(ground_[sample]);
    if (!flow_.empty())
      flow += weights[corner] * double(std::clamp(flow_[sample], 0.F, 1.F));
    if (!wet_[sample]) {
      continue;
    }
    double coverage = 0.0;
    for (std::size_t i = 0; i < corners.size(); ++i) {
      const auto neighbour = corners[i];
      coverage +=
          weights[i] * terrain_water_detail::waterOwnershipMask(
                           wet_[neighbour], body_[neighbour], body_[sample]);
    }
    if (!(coverage > 0.0))
      continue;
    // Keep the winning body's authored plane; interpolating different bodies'
    // levels would introduce a slope neither body nor its mesh contains.
    if (owner == terrain_water_detail::WaterLevelField::noBody ||
        body_[sample] > owner) {
      owner = body_[sample];
      level = level_[sample];
    }
  }
  if (owner == terrain_water_detail::WaterLevelField::noBody) {
    return TerrainWaterQuery{};
  }
  const auto &triangle =
      terrain_water_detail::waterCellTriangles[u + v <= 1 ? 0 : 1];
  const bool hasWetCorner = std::ranges::any_of(triangle, [&](auto corner) {
    const auto sample = corners[corner];
    return wet_[sample] && body_[sample] == owner && ground_[sample] < level;
  });
  if (!hasWetCorner)
    return TerrainWaterQuery{};

  TerrainWaterQuery query;
  query.surfaceHeight = float(level);
  query.groundHeight = float(ground);
  query.depth = float(std::max(0.0, level - ground));
  query.submerged = query.depth > 0.F;
  if (!query.submerged) {
    // Dry means dry, including exactly on the waterline: a body id on a point
    // with no water in it would let a caller ask a dry question of a river.
    return query;
  }
  query.underwater = localPosition.y >= query.groundHeight &&
                     localPosition.y < query.surfaceHeight;
  if (owner < ids_.size()) {
    query.bodyId = ids_[owner];
    // Still water is still by definition. A lake or an ocean fed by a river has
    // flow at its mouth in the simulation of record, and reporting it here
    // would make a swimmer depend on a drain rate nobody authored.
    if (kinds_[owner] == TerrainWaterBody::River)
      query.flowSpeed =
          float(std::clamp(flow, 0.0, 1.0)) * terrainWaterReferenceFlowSpeed;
    query.swimmable = query.depth >= terrainWaterMinSwimDepth &&
                      query.flowSpeed <= terrainWaterMaxSwimFlow;
  }
  return query;
}

std::optional<TerrainWaterQuery>
sampleTerrainWaterAt(const TerrainWaterAuthoring &authoring,
                     const TerrainWaterResult &result,
                     const TerrainMasks *masks, Vec3 worldPosition, int cellsX,
                     int cellsZ, Vec2 size) {
  const auto context = TerrainWaterQueryContext::build(authoring, result, masks,
                                                       cellsX, cellsZ, size);
  if (!context) {
    return std::nullopt;
  }
  return context->sample(worldPosition);
}
} // namespace demi::runtime
