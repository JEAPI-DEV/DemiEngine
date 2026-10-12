#include "demi/runtime/render/bgfx3d/TerrainDrawSelection3D.h"

#include "demi/runtime/terrain/TerrainRenderLod.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace demi::runtime::render {

namespace {

float dot(Vec3 first, Vec3 second) {
  return first.x * second.x + first.y * second.y + first.z * second.z;
}

Vec3 cross(Vec3 first, Vec3 second) {
  return {first.y * second.z - first.z * second.y,
          first.z * second.x - first.x * second.z,
          first.x * second.y - first.y * second.x};
}

std::optional<Vec3> normalized(Vec3 value) {
  const float length = std::hypot(value.x, value.y, value.z);
  if (!std::isfinite(length) || !(length > 0)) {
    return std::nullopt;
  }
  return Vec3{value.x / length, value.y / length, value.z / length};
}

// Full and reduced terrain differ only in local height. The projected error
// therefore follows transformed local Y, rather than an arbitrary displacement
// in whichever world axis has the largest scale.
float projectedPixelsPerHeight(const PreparedTerrainChunkLod &chunk,
                               const WorldTransform3D &transform, Vec3 meshSize,
                               const BgfxCameraFrame3D &camera) {
  const auto forward = normalized(camera.forward);
  if (!forward) {
    return std::numeric_limits<float>::infinity();
  }
  const auto right = normalized(cross(camera.up, *forward));
  if (!right) {
    return std::numeric_limits<float>::infinity();
  }
  const auto up = cross(*forward, *right);
  auto scaled = transform;
  scaled.scale = {transform.scale.x * meshSize.x,
                  transform.scale.y * meshSize.y,
                  transform.scale.z * meshSize.z};
  const auto heightDirection =
      transformDirection3D(scaled, {0, scaled.scale.y, 0});
  const float heightX = std::abs(dot(heightDirection, *right));
  const float heightY = std::abs(dot(heightDirection, up));
  if (!camera.camera.perspective) {
    if (!(camera.camera.orthographicSize > 0)) {
      return std::numeric_limits<float>::infinity();
    }
    return float(camera.viewportHeight) * std::hypot(heightX, heightY) /
           (2.0F * camera.camera.orthographicSize);
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
        minimumDepth = std::min(minimumDepth, dot(relative, *forward));
        maximumX = std::max(maximumX, std::abs(dot(relative, *right)));
        maximumY = std::max(maximumY, std::abs(dot(relative, up)));
      }
    }
  }
  const float tangent =
      std::tan(camera.camera.fov * std::numbers::pi_v<float> / 360.0F);
  if (!std::isfinite(minimumDepth) ||
      !(minimumDepth > camera.camera.nearClip) || !std::isfinite(tangent) ||
      !(tangent > 0)) {
    return std::numeric_limits<float>::infinity();
  }
  const float heightDepth = std::abs(dot(heightDirection, *forward));
  // Perspective projection is (f*x/z, f*y/z). Bound its directional
  // derivative over the transformed chunk box. Both terrain surfaces and the
  // height segment between them lie inside this convex box, so integrating
  // the bound also covers finite errors and tilted/nonuniform placements.
  const float horizontalBound = heightX + maximumX * heightDepth / minimumDepth;
  const float verticalBound = heightY + maximumY * heightDepth / minimumDepth;
  const float focalPixels = float(camera.viewportHeight) / (2.0F * tangent);
  return focalPixels * std::hypot(horizontalBound, verticalBound) /
         minimumDepth;
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
