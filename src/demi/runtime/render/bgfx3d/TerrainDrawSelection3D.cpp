#include "demi/runtime/render/bgfx3d/TerrainDrawSelection3D.h"

#include "demi/runtime/camera/Camera3DMath.h"
#include "demi/runtime/math/VectorMath.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace demi::runtime::render {

namespace {

// Full and reduced terrain differ only in local height. The projected error
// therefore follows transformed local Y, rather than an arbitrary displacement
// in whichever world axis has the largest scale.
float projectedPixelsPerHeight(const PreparedTerrainChunkLod &chunk,
                               const WorldTransform3D &transform, Vec3 meshSize,
                               const BgfxCameraFrame3D &camera) {
  const auto projection = cameraProjection3D(
      camera.camera, camera.position, camera.forward, camera.up,
      {float(camera.viewportWidth), float(camera.viewportHeight)});
  if (!projection) {
    return std::numeric_limits<float>::infinity();
  }
  auto scaled = transform;
  scaled.scale = {transform.scale.x * meshSize.x,
                  transform.scale.y * meshSize.y,
                  transform.scale.z * meshSize.z};
  const auto heightDirection =
      transformDirection3D(scaled, {0, scaled.scale.y, 0});
  const float heightX = std::abs(math::dot(heightDirection, projection->right));
  const float heightY = std::abs(math::dot(heightDirection, projection->up));
  if (!camera.camera.perspective) {
    if (!(camera.camera.orthographicSize > 0)) {
      return std::numeric_limits<float>::infinity();
    }
    return projection->pixelsPerUnit() * std::hypot(heightX, heightY);
  }

  float minimumDepth = std::numeric_limits<float>::infinity();
  float maximumX = 0;
  float maximumY = 0;
  const auto position = camera.lodPosition.value_or(camera.position);
  for (const float x : {chunk.boundsMin.x, chunk.boundsMax.x}) {
    for (const float y : {chunk.boundsMin.y, chunk.boundsMax.y}) {
      for (const float z : {chunk.boundsMin.z, chunk.boundsMax.z}) {
        const auto world = transformPoint3D(scaled, {x, y, z});
        const Vec3 relative{world.x - position.x, world.y - position.y,
                            world.z - position.z};
        minimumDepth =
            std::min(minimumDepth, math::dot(relative, projection->forward));
        maximumX = std::max(maximumX,
                            std::abs(math::dot(relative, projection->right)));
        maximumY =
            std::max(maximumY, std::abs(math::dot(relative, projection->up)));
      }
    }
  }
  if (!std::isfinite(minimumDepth) ||
      !(minimumDepth > camera.camera.nearClip)) {
    return std::numeric_limits<float>::infinity();
  }
  const float heightDepth =
      std::abs(math::dot(heightDirection, projection->forward));
  // Perspective projection is (f*x/z, f*y/z). Bound its directional
  // derivative over the transformed chunk box. Both terrain surfaces and the
  // height segment between them lie inside this convex box, so integrating
  // the bound also covers finite errors and tilted/nonuniform placements.
  const float horizontalBound = heightX + maximumX * heightDepth / minimumDepth;
  const float verticalBound = heightY + maximumY * heightDepth / minimumDepth;
  return projection->pixelsPerUnit(minimumDepth) *
         std::hypot(horizontalBound, verticalBound);
}

} // namespace

std::size_t selectTerrainDrawLevel3D(const PreparedTerrainChunkLod &chunk,
                                     const WorldTransform3D &transform,
                                     Vec3 meshSize,
                                     const BgfxCameraFrame3D &camera,
                                     float errorPixels) {
  if (!std::isfinite(errorPixels) || errorPixels <= 0 ||
      camera.viewportHeight == 0) {
    return 0;
  }
  const float pixelsPerHeight =
      projectedPixelsPerHeight(chunk, transform, meshSize, camera);
  if (!std::isfinite(pixelsPerHeight)) {
    return 0;
  }
  for (std::size_t level = chunk.geometricErrors.size(); level-- > 1;) {
    if (chunk.triangleCounts[level] >= chunk.triangleCounts[0]) {
      continue;
    }
    const float error = chunk.geometricErrors[level] * pixelsPerHeight;
    if (std::isfinite(error) && error >= 0 && error <= errorPixels) {
      return level;
    }
  }
  return 0;
}

void TerrainDrawSelection3D::beginPresentation() {
  ++presentation_;
  for (auto &[camera, owners] : cameras_) {
    for (auto &[owner, chunks] : owners) {
      std::erase_if(chunks, [this](const auto &entry) {
        return entry.second.chunk.expired() ||
               presentation_ - entry.second.lastPresentation > 8;
      });
    }
    std::erase_if(owners,
                  [](const auto &entry) { return entry.second.empty(); });
  }
  std::erase_if(cameras_,
                [](const auto &entry) { return entry.second.empty(); });
}

void TerrainDrawSelection3D::clear() {
  cameras_.clear();
  presentation_ = 0;
}

std::size_t TerrainDrawSelection3D::select(
    const std::shared_ptr<const PreparedTerrainChunkLod> &chunk,
    const WorldTransform3D &transform, Vec3 meshSize,
    const BgfxCameraFrame3D &camera, float errorPixels,
    const std::string &owner) {
  const auto key = camera.cameraId.empty()
                       ? "view:" + std::to_string(camera.viewId)
                       : camera.cameraId;
  auto &states = cameras_[key][owner];
  auto [entry, inserted] = states.try_emplace(chunk.get());
  auto &state = entry->second;
  const auto desired = selectTerrainDrawLevel3D(*chunk, transform, meshSize,
                                                camera, errorPixels);
  if (inserted || state.chunk.expired()) {
    state.level = desired;
  } else if (desired < state.level) {
    state.level = desired;
  } else if (desired > state.level) {
    const auto withHeadroom = selectTerrainDrawLevel3D(
        *chunk, transform, meshSize, camera, errorPixels * 0.85F);
    state.level = std::max(state.level, withHeadroom);
  }
  state.chunk = chunk;
  state.lastPresentation = presentation_;
  return state.level;
}

} // namespace demi::runtime::render
