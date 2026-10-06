#include "demi/runtime/terrain/TerrainScatterConstraints.h"
#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

namespace demi::runtime {
namespace {

// Reason text, indexed by the Block bit it belongs to. Static storage, so
// reason() can hand out a view without allocating or owning anything.
constexpr std::string_view blockNames[] = {
    "footprint",        "road",
    "protected area",   "painted exclusion",
    "terrain exclusion", "water surface",
    "water",            "slope",
};
static_assert(std::size(blockNames) == 8,
              "One reason name per blocking kind, in precedence order");

// A point within this distance of a footprint edge is inside it. Small and
// absolute: it exists to make the boundary answer well-defined, not to inflate
// the building.
constexpr float boundaryEpsilon = 1e-4F;

// Buckets per axis, capped. The cap is what bounds the cost of a shape covering
// the whole field: every such shape costs at most buckets^2 references, so an
// authored document with pathological regions still cannot allocate without
// limit. A finer grid only helps shapes that are small relative to the field,
// which is what roads and building footprints are.
int bucketCountFor(std::size_t samples) {
  const auto wanted = std::size_t(
      std::lround(std::sqrt(static_cast<double>(samples) / 16.F)));
  return static_cast<int>(std::clamp<std::size_t>(wanted, 1, 128));
}

float distanceToSegment(Vec2 point, Vec2 a, Vec2 b) {
  const Vec2 delta{b.x - a.x, b.y - a.y};
  const float lengthSquared = delta.x * delta.x + delta.y * delta.y;
  // A degenerate segment is a point, not a division by zero.
  if (lengthSquared <= 0.F)
    return std::hypot(point.x - a.x, point.y - a.y);
  const float t = std::clamp(((point.x - a.x) * delta.x +
                              (point.y - a.y) * delta.y) /
                                 lengthSquared,
                             0.F, 1.F);
  return std::hypot(point.x - (a.x + delta.x * t), point.y - (a.y + delta.y * t));
}

// Winding-agnostic crossing test, with the boundary answered first because ray
// casting has no defined answer for a point exactly on an edge and a solid
// footprint must include its own walls.
bool pointInPolygon(Vec2 point, const std::vector<Vec2> &polygon) {
  if (polygon.size() < 3)
    return false;
  const auto count = polygon.size();
  for (std::size_t i = 0, j = count - 1; i < count; j = i++)
    if (distanceToSegment(point, polygon[j], polygon[i]) <= boundaryEpsilon)
      return true;
  bool inside = false;
  for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
    const Vec2 a = polygon[i];
    const Vec2 b = polygon[j];
    if ((a.y > point.y) == (b.y > point.y))
      continue;
    const float t = (point.y - a.y) / (b.y - a.y);
    if (point.x < a.x + t * (b.x - a.x))
      inside = !inside;
  }
  return inside;
}

bool finite(float value) { return std::isfinite(value); }

} // namespace

TerrainScatterConstraints TerrainScatterConstraints::build(
    const HeightField &field, const TerrainRecipe &recipe,
    const TerrainMasks *masks, const TerrainWaterLevel &water,
    const TerrainScatterConstraintOptions &options) {
  if (field.cellsX <= 0 || field.cellsZ <= 0)
    throw std::invalid_argument(
        "Terrain scatter constraints need a positive cell grid");
  if (!(field.size.x > 0.F) || !(field.size.y > 0.F))
    throw std::invalid_argument(
        "Terrain scatter constraints need a positive field size");
  const auto samples =
      std::size_t(field.cellsZ + 1) * std::size_t(field.cellsX + 1);
  if (field.heights.size() != samples)
    throw std::invalid_argument(
        "Terrain scatter constraints do not match the heightfield");
  if (masks != nullptr && masks->count() != samples)
    throw std::invalid_argument(
        "Terrain scatter constraints need masks on the field's grid");

  TerrainScatterConstraints constraints;
  constraints.size_ = field.size;
  constraints.cellsX_ = field.cellsX;
  constraints.cellsZ_ = field.cellsZ;
  constraints.seaLevel_ =
      water.authored ? water.seaLevel : TerrainRuleContextBuilder::seaLevel(recipe);
  constraints.maximumSlope_ = options.maximumSlopeDegrees;
  constraints.paintedExclusionThreshold_ = options.paintedExclusionThreshold;
  constraints.buckets_ = bucketCountFor(samples);
  constraints.bucketSizeX_ = field.size.x / float(constraints.buckets_);
  constraints.bucketSizeZ_ = field.size.y / float(constraints.buckets_);
  constraints.mask_.assign(samples, 0);
  constraints.painted_.assign(samples, 0.F);
  constraints.shapeBuckets_.assign(
      std::size_t(constraints.buckets_) * std::size_t(constraints.buckets_), {});

  const float stepX = field.size.x / float(field.cellsX);
  const float stepZ = field.size.y / float(field.cellsZ);
  for (int z = 0; z <= field.cellsZ; ++z)
    for (int x = 0; x <= field.cellsX; ++x) {
      const auto cell = field.index(x, z);
      const float height = field.heights[cell];
      auto blockers = std::uint8_t{0};
      // Both water kinds are evaluated rather than short-circuited, because
      // "water surface" outranks "water" and an authored body can sit above the
      // sea level as easily as below it.
      if (options.submerged &&
          options.submerged({x * stepX, height, z * stepZ}, height))
        blockers |= blockWaterSurface;
      if (!(height > constraints.seaLevel_))
        blockers |= blockWater;
      if (masks != nullptr && masks->slope[cell] >= options.maximumSlopeDegrees)
        blockers |= blockSlope;
      if (field.exclusions[cell] >= options.terrainExclusionThreshold)
        blockers |= blockTerrainExclusion;
      constraints.mask_[cell] = blockers;
    }
  return constraints;
}

Vec2 TerrainScatterConstraints::cellPosition(std::size_t cell) const noexcept {
  const auto perRow = std::size_t(cellsX_ + 1);
  const auto x = float(double(cell % perRow) * size_.x / cellsX_);
  const auto z = float(double(cell / perRow) * size_.y / cellsZ_);
  return {x, z};
}

void TerrainScatterConstraints::insertShape(std::uint32_t shape, float minX,
                                            float maxX, float minZ, float maxZ) {
  // Clamped to the field, so a shape larger than the terrain still answers for
  // every cell it can reach and never allocates buckets nobody can query.
  const auto firstX = std::clamp(int(std::floor(minX / bucketSizeX_)), 0, buckets_ - 1);
  const auto lastX = std::clamp(int(std::floor(maxX / bucketSizeX_)), 0, buckets_ - 1);
  const auto firstZ = std::clamp(int(std::floor(minZ / bucketSizeZ_)), 0, buckets_ - 1);
  const auto lastZ = std::clamp(int(std::floor(maxZ / bucketSizeZ_)), 0, buckets_ - 1);
  for (int z = firstZ; z <= lastZ; ++z)
    for (int x = firstX; x <= lastX; ++x)
      shapeBuckets_[std::size_t(z) * std::size_t(buckets_) +
                    std::size_t(x)]
          .push_back(shape);
}

std::uint8_t TerrainScatterConstraints::spatialBlockers(Vec2 point) const {
  if (shapes_.empty())
    return 0;
  const auto bucketX = std::clamp(int(std::floor(point.x / bucketSizeX_)), 0,
                                  buckets_ - 1);
  const auto bucketZ = std::clamp(int(std::floor(point.y / bucketSizeZ_)), 0,
                                  buckets_ - 1);
  const auto &candidates = shapeBuckets_[std::size_t(bucketZ) *
                                             std::size_t(buckets_) +
                                         std::size_t(bucketX)];
  std::uint8_t blockers = 0;
  // Every candidate is tested rather than returning on the first hit: the reason
  // is the highest-precedence blocker, so a road inside a protected area has to
  // report the road.
  for (const auto index : candidates) {
    const Shape &shape = shapes_[index];
    switch (shape.kind) {
    case ShapeKind::Area:
      if (std::hypot(point.x - shape.center.x, point.y - shape.center.y) <=
          shape.radius)
        blockers |= blockProtectedArea;
      break;
    case ShapeKind::Road: {
      const auto segments = shape.centreline.size() - 1;
      for (std::size_t s = 0; s < segments; ++s) {
        const Vec3 a = shape.centreline[s];
        const Vec3 b = shape.centreline[s + 1];
        // Distance to the polyline, not to its bounding box: a road that bends
        // back on itself must not reject the ground in the box it happens to
        // enclose, which is exactly the failure a bounding-box test has.
        if (distanceToSegment(point, {a.x, a.z}, {b.x, b.z}) <= shape.halfWidth) {
          blockers |= blockRoad;
          break;
        }
      }
      break;
    }
    case ShapeKind::Footprint:
      if (pointInPolygon(point, shape.polygon))
        blockers |= blockFootprint;
      break;
    }
  }
  return blockers;
}

std::uint8_t TerrainScatterConstraints::cellBlockers(std::size_t cell) const {
  if (cell >= mask_.size())
    return 0;
  auto blockers = mask_[cell];
  if (painted_[cell] >= paintedExclusionThreshold_)
    blockers |= blockPaintedExclusion;
  blockers |= spatialBlockers(cellPosition(cell));
  return blockers;
}

bool TerrainScatterConstraints::blocked(std::size_t cell) const {
  return cellBlockers(cell) != 0;
}

std::string_view TerrainScatterConstraints::reason(std::size_t cell) const {
  const auto blockers = cellBlockers(cell);
  if (blockers == 0)
    return {};
  // First set bit, which is the documented precedence order.
  for (std::size_t bit = 0; bit < std::size(blockNames); ++bit)
    if (blockers & (1U << bit))
      return blockNames[bit];
  return {};
}

std::size_t TerrainScatterConstraints::countBlocked() const {
  std::size_t count = 0;
  for (std::size_t cell = 0; cell < mask_.size(); ++cell)
    count += blocked(cell) ? 1U : 0U;
  return count;
}

std::size_t TerrainScatterConstraints::blockedCount() const {
  if (blockedDirty_) {
    cachedBlocked_ = countBlocked();
    blockedDirty_ = false;
  }
  return cachedBlocked_;
}

void TerrainScatterConstraints::addProtectedArea(Vec2 center, float radius) {
  if (!finite(center.x) || !finite(center.y) || !finite(radius) || radius <= 0.F)
    return;
  const auto index = static_cast<std::uint32_t>(shapes_.size());
  Shape shape;
  shape.kind = ShapeKind::Area;
  shape.center = center;
  shape.radius = radius;
  shapes_.push_back(std::move(shape));
  insertShape(index, center.x - radius, center.x + radius, center.y - radius,
              center.y + radius);
  blockedDirty_ = true;
}

void TerrainScatterConstraints::addRoad(std::vector<Vec3> centreline,
                                        float width) {
  if (centreline.size() < 2 || !finite(width) || width <= 0.F)
    return;
  float minX = centreline.front().x;
  float maxX = minX;
  float minZ = centreline.front().z;
  float maxZ = minZ;
  for (const auto &point : centreline) {
    if (!finite(point.x) || !finite(point.z))
      return;
    minX = std::min(minX, point.x);
    maxX = std::max(maxX, point.x);
    minZ = std::min(minZ, point.z);
    maxZ = std::max(maxZ, point.z);
  }
  const float reach = width * 0.5F;
  const auto index = static_cast<std::uint32_t>(shapes_.size());
  Shape shape;
  shape.kind = ShapeKind::Road;
  shape.centreline = std::move(centreline);
  shape.halfWidth = reach;
  shapes_.push_back(std::move(shape));
  insertShape(index, minX - reach, maxX + reach, minZ - reach, maxZ + reach);
  blockedDirty_ = true;
}

void TerrainScatterConstraints::addFootprint(std::vector<Vec2> polygon) {
  if (polygon.size() < 3)
    return;
  float minX = polygon.front().x;
  float maxX = minX;
  float minZ = polygon.front().y;
  float maxZ = minZ;
  for (const auto &point : polygon) {
    if (!finite(point.x) || !finite(point.y))
      return;
    minX = std::min(minX, point.x);
    maxX = std::max(maxX, point.x);
    minZ = std::min(minZ, point.y);
    maxZ = std::max(maxZ, point.y);
  }
  const auto index = static_cast<std::uint32_t>(shapes_.size());
  Shape shape;
  shape.kind = ShapeKind::Footprint;
  shape.polygon = std::move(polygon);
  shapes_.push_back(std::move(shape));
  insertShape(index, minX, maxX, minZ, maxZ);
  blockedDirty_ = true;
}

void TerrainScatterConstraints::addExclusionWeight(std::size_t cell,
                                                    float weight) {
  if (cell >= painted_.size() || !finite(weight))
    return;
  painted_[cell] = weight;
  blockedDirty_ = true;
}

} // namespace demi::runtime
