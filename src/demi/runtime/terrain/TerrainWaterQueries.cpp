#include "demi/runtime/terrain/TerrainWaterQueries.h"

#include "demi/runtime/terrain/TerrainGeneration.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace demi::runtime {
namespace {

std::size_t sampleCount(int cellsX, int cellsZ) {
  return (std::size_t(cellsX) + 1) * (std::size_t(cellsZ) + 1);
}

// The drainage mask belongs to the generation grid and publishes no grid of its
// own, so only a mask that matches the field can be addressed sample for
// sample. A mask that does not match is refused by build() rather than
// resampled: guessing which of its samples corresponds to which cell would be
// inventing drainage, and inventing drainage here means inventing how fast a
// river pushes a character downstream.
void readFlow(const TerrainMasks *masks, std::size_t count,
              std::vector<float> &flow) {
  flow.assign(count, 0.F);
  if (masks == nullptr)
    return;
  for (std::size_t sample = 0; sample < count; ++sample)
    flow[sample] = std::clamp(masks->flow[sample], 0.F, 1.F);
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
  const auto field = terrain_water_detail::buildWaterLevelField(
      size, cellsX, cellsZ, result.carvedHeights, authoring);
  if (field.samples() != count) {
    return std::nullopt;
  }

  TerrainWaterQueryContext context;
  context.size_ = size;
  context.cellsX_ = cellsX;
  context.cellsZ_ = cellsZ;
  context.ground_ = result.carvedHeights;
  context.level_ = field.level;
  context.wet_ = field.wet;
  context.body_ = field.body;
  readFlow(masks, count, context.flow_);
  context.ids_.reserve(authoring.bodies.size());
  for (const auto &body : authoring.bodies) {
    context.ids_.push_back(body.id);
    context.kinds_.push_back(body.kind);
  }
  return context;
}

std::optional<TerrainWaterQuery>
TerrainWaterQueryContext::sample(Vec3 worldPosition) const {
  if (level_.empty() || !std::isfinite(worldPosition.x) ||
      !std::isfinite(worldPosition.y) || !std::isfinite(worldPosition.z)) {
    return std::nullopt;
  }
  if (worldPosition.x < 0 || worldPosition.z < 0 || worldPosition.x > size_.x ||
      worldPosition.z > size_.y) {
    return std::nullopt;
  }
  const auto cellX =
      std::clamp(int(std::floor(double(worldPosition.x) / size_.x * cellsX_)),
                 0, cellsX_ - 1);
  const auto cellZ =
      std::clamp(int(std::floor(double(worldPosition.z) / size_.y * cellsZ_)),
                 0, cellsZ_ - 1);
  // The same corner positions the field itself is built from, so a query lands
  // on the same cell the carve did instead of a float-shifted neighbour.
  const auto cornerX = [&](int x) {
    return float(double(x) * size_.x / cellsX_);
  };
  const auto cornerZ = [&](int z) {
    return float(double(z) * size_.y / cellsZ_);
  };
  const auto u = double(worldPosition.x - cornerX(cellX)) /
                 (double(cornerX(cellX + 1)) - double(cornerX(cellX)));
  const auto v = double(worldPosition.z - cornerZ(cellZ)) /
                 (double(cornerZ(cellZ + 1)) - double(cornerZ(cellZ)));

  const auto columns = std::size_t(cellsX_) + 1;
  const std::array<std::size_t, 4> corners{
      std::size_t(cellZ) * columns + std::size_t(cellX),
      std::size_t(cellZ) * columns + std::size_t(cellX + 1),
      std::size_t(cellZ + 1) * columns + std::size_t(cellX),
      std::size_t(cellZ + 1) * columns + std::size_t(cellX + 1)};
  const std::array<double, 4> weights{(1.0 - u) * (1.0 - v), u * (1.0 - v),
                                      (1.0 - u) * v, u * v};

  double ground = 0.0;
  double level = 0.0;
  double levelWeight = 0.0;
  double flow = 0.0;
  auto owner = terrain_water_detail::WaterLevelField::noBody;
  for (std::size_t corner = 0; corner < corners.size(); ++corner) {
    const auto sample = corners[corner];
    ground += weights[corner] * double(ground_[sample]);
    flow += weights[corner] * double(flow_[sample]);
    if (!wet_[sample]) {
      continue;
    }
    // Weighted over the wet corners only. Normalising over all four would drag
    // the level down towards a dry neighbour and shrink the body by a cell;
    // ignoring the weights would make a level field of stepped values answer
    // with one corner's height. The surface is a plane, so the blend is a
    // blend of planes, and it is the ground that crosses it that decides where
    // the shoreline is.
    level += weights[corner] * double(level_[sample]);
    levelWeight += weights[corner];
    // Authored priority order decides an overlap, here as it did on the mesh.
    if (owner == terrain_water_detail::WaterLevelField::noBody ||
        body_[sample] > owner) {
      owner = body_[sample];
    }
  }
  if (levelWeight <= 0.0) {
    return TerrainWaterQuery{};
  }

  TerrainWaterQuery query;
  query.depth = float(std::max(0.0, level / levelWeight - ground));
  query.submerged = query.depth > 0.F;
  if (!query.submerged) {
    // Dry means dry, including exactly on the waterline: a body id on a point
    // with no water in it would let a caller ask a dry question of a river.
    return query;
  }
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
