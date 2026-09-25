#include "editor/EditorViewportProjection.h"

#include "editor/EditorEntityBounds3D.h"

#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace demi::editor {
namespace {

struct Ray {
  runtime::Vec3 origin;
  runtime::Vec3 direction;
};

float dot(const runtime::Vec3 left, const runtime::Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

runtime::Vec3 add(const runtime::Vec3 left, const runtime::Vec3 right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

runtime::Vec3 subtract(const runtime::Vec3 left, const runtime::Vec3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

runtime::Vec3 multiply(const runtime::Vec3 value, const float scalar) {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

runtime::Vec3 cross(const runtime::Vec3 left, const runtime::Vec3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

float length(const runtime::Vec3 value) { return std::sqrt(dot(value, value)); }

runtime::Vec3 normalized(const runtime::Vec3 value,
                         const runtime::Vec3 fallback = {}) {
  const float magnitude = length(value);
  return magnitude > 0.00001F ? multiply(value, 1.0F / magnitude) : fallback;
}

struct CameraBasis {
  runtime::Vec3 forward;
  runtime::Vec3 right;
  runtime::Vec3 up;
};

CameraBasis cameraBasis(const EditorSceneViewCamera &camera) {
  const runtime::Vec3 forward =
      normalized(camera.forward, {0.0F, 0.0F, 1.0F});
  // Bgfx uses a right-handed look-at matrix. Its view direction is the
  // inverse of the gameplay-facing forward vector, so screen-right is
  // forward x up (not up x forward).
  const runtime::Vec3 right =
      normalized(cross(forward, camera.up), {-1.0F, 0.0F, 0.0F});
  const runtime::Vec3 up =
      normalized(cross(right, forward), {0.0F, 1.0F, 0.0F});
  return {.forward = forward, .right = right, .up = up};
}

Ray cameraRay(const EditorSceneViewCamera &camera, const runtime::Vec2 position,
              const runtime::Vec2 viewport) {
  const float width = std::max(viewport.x, 1.0F);
  const float height = std::max(viewport.y, 1.0F);
  const float ndcX = position.x * 2.0F / width - 1.0F;
  const float ndcY = 1.0F - position.y * 2.0F / height;
  const CameraBasis basis = cameraBasis(camera);
  const runtime::Vec3 forward = basis.forward;
  const runtime::Vec3 right = basis.right;
  const runtime::Vec3 up = basis.up;
  if (!camera.projection.perspective) {
    const float halfHeight =
        std::max(camera.projection.orthographicSize * 0.5F, 0.01F);
    const float halfWidth = halfHeight * width / height;
    return {.origin =
                add(camera.position, add(multiply(right, ndcX * halfWidth),
                                         multiply(up, ndcY * halfHeight))),
            .direction = forward};
  }
  constexpr float DegreesToRadians = 0.01745329251994329577F;
  const float tangent =
      std::tan(std::clamp(camera.projection.fov, 1.0F, 179.0F) *
               DegreesToRadians * 0.5F);
  return {.origin = camera.position,
          .direction = normalized(
              add(forward, add(multiply(right, ndcX * tangent * width / height),
                               multiply(up, ndcY * tangent))),
              forward)};
}

std::optional<float> rayBox(const Ray &ray, const runtime::Vec3 minimum,
                            const runtime::Vec3 maximum) {
  float nearDistance = 0.0F;
  float farDistance = std::numeric_limits<float>::max();
  const auto clip = [&](const float origin, const float direction,
                        const float low, const float high) {
    if (std::abs(direction) < 0.000001F)
      return origin >= low && origin <= high;
    float first = (low - origin) / direction;
    float second = (high - origin) / direction;
    if (first > second)
      std::swap(first, second);
    nearDistance = std::max(nearDistance, first);
    farDistance = std::min(farDistance, second);
    return nearDistance <= farDistance;
  };
  if (!clip(ray.origin.x, ray.direction.x, minimum.x, maximum.x) ||
      !clip(ray.origin.y, ray.direction.y, minimum.y, maximum.y) ||
      !clip(ray.origin.z, ray.direction.z, minimum.z, maximum.z))
    return std::nullopt;
  return nearDistance;
}

} // namespace

std::optional<runtime::Vec2>
projectScenePoint3D(const EditorSceneViewCamera &camera,
                    const runtime::Vec3 worldPoint,
                    const runtime::Vec2 viewportSize) {
  const float width = std::max(viewportSize.x, 1.0F);
  const float height = std::max(viewportSize.y, 1.0F);
  const CameraBasis basis = cameraBasis(camera);
  const runtime::Vec3 forward = basis.forward;
  const runtime::Vec3 right = basis.right;
  const runtime::Vec3 up = basis.up;
  const runtime::Vec3 offset = subtract(worldPoint, camera.position);
  const float depth = dot(offset, forward);
  if (depth <= std::max(camera.projection.nearClip, 0.001F))
    return std::nullopt;
  float ndcX = 0.0F;
  float ndcY = 0.0F;
  if (camera.projection.perspective) {
    constexpr float DegreesToRadians = 0.01745329251994329577F;
    const float tangent =
        std::tan(std::clamp(camera.projection.fov, 1.0F, 179.0F) *
                 DegreesToRadians * 0.5F);
    ndcX = dot(offset, right) / (depth * tangent * width / height);
    ndcY = dot(offset, up) / (depth * tangent);
  } else {
    const float halfHeight =
        std::max(camera.projection.orthographicSize * 0.5F, 0.01F);
    ndcX = dot(offset, right) / (halfHeight * width / height);
    ndcY = dot(offset, up) / halfHeight;
  }
  return runtime::Vec2{(ndcX + 1.0F) * width * 0.5F,
                       (1.0F - ndcY) * height * 0.5F};
}

runtime::Vec2 projectSceneDirection3D(
    const EditorSceneViewCamera &camera,
    const runtime::Vec3 worldDirection) {
  const CameraBasis basis = cameraBasis(camera);
  return {dot(worldDirection, basis.right),
          -dot(worldDirection, basis.up)};
}

std::optional<runtime::Vec3>
intersectSceneGroundPlane3D(const EditorSceneViewCamera &camera,
                            const runtime::Vec2 viewportPosition,
                            const runtime::Vec2 viewportSize) {
  if (viewportSize.x <= 0.0F || viewportSize.y <= 0.0F)
    return std::nullopt;
  const Ray ray = cameraRay(camera, viewportPosition, viewportSize);
  if (std::abs(ray.direction.y) <= 0.000001F)
    return std::nullopt;
  const float distance = -ray.origin.y / ray.direction.y;
  if (distance < 0.0F)
    return std::nullopt;
  return add(ray.origin, multiply(ray.direction, distance));
}

std::optional<std::string> pickSceneEntity3D(
    const runtime::World &world, const EditorSceneViewCamera &camera,
    const runtime::Vec2 viewportPosition, const runtime::Vec2 viewportSize) {
  const Ray ray = cameraRay(camera, viewportPosition, viewportSize);
  float nearestDistance = std::max(camera.projection.farClip, 0.0F);
  std::optional<std::string> nearest;

  for (const runtime::Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    const auto bounds = editorEntityBounds3D(world, entity);
    if (!bounds)
      continue;
    const auto considerTransform = [&](const runtime::WorldTransform3D &pose) {
      const Ray local{
          .origin = runtime::inverseTransformPoint3D(pose, ray.origin),
          .direction = runtime::inverseTransformVector3D(pose, ray.direction)};
      const auto distance =
          rayBox(local, bounds->local.minimum, bounds->local.maximum);
      if (distance && *distance > camera.projection.nearClip &&
          *distance < nearestDistance) {
        nearestDistance = *distance;
        nearest = entity.id;
      }
    };

    for (const runtime::WorldTransform3D &transform : bounds->worldTransforms)
      considerTransform(transform);
  }
  return nearest;
}

} // namespace demi::editor
