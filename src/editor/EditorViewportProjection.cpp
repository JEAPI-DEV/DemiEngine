#include "editor/EditorViewportProjection.h"

#include "editor/EditorEntityBounds3D.h"

#include "demi/runtime/camera/Camera3DMath.h"
#include "demi/runtime/math/VectorMath.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainWorld.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace demi::editor {
namespace {

using Ray = EditorViewportRay;

using runtime::math::add;
using runtime::math::dot;
using runtime::math::scale;
using runtime::math::subtract;

runtime::Vec3 normalizedOr(runtime::Vec3 value, runtime::Vec3 fallback) {
  const float magnitude = runtime::math::length(value);
  return magnitude > 0.00001F ? scale(value, 1.0F / magnitude) : fallback;
}

std::optional<runtime::CameraProjection3D>
editorProjection(const EditorSceneViewCamera &camera, runtime::Vec2 viewport) {
  // Only projection scalars are needed; do not copy asset IDs or render masks
  // for every overlay point and picking ray.
  runtime::Camera3DComponent settings;
  settings.fov = camera.projection.fov;
  settings.perspective = camera.projection.perspective;
  // Authoring overlays reject the exact near plane and project beyond far_clip.
  settings.nearClip = std::nextafter(std::max(camera.projection.nearClip, 0.001F),
                                     std::numeric_limits<float>::infinity());
  settings.farClip = std::numeric_limits<float>::max();
  settings.orthographicSize = std::max(camera.projection.orthographicSize, 0.02F);
  const auto forward = normalizedOr(camera.forward, {0, 0, 1});
  auto projection = runtime::cameraProjection3D(settings, camera.position,
                                                forward, camera.up, viewport);
  if (projection && runtime::math::length(
                        runtime::math::cross(forward, camera.up)) > 0.00001F) {
    return projection;
  }
  // Preserve the editor's established basis when up and forward are collinear.
  auto fallback = runtime::cameraProjection3D(settings, camera.position,
                                              {0, 0, 1}, {0, 1, 0}, viewport);
  if (!fallback) {
    return std::nullopt;
  }
  fallback->forward = forward;
  fallback->right = {-1, 0, 0};
  fallback->up =
      normalizedOr(runtime::math::cross(fallback->right, forward), {0, 1, 0});
  return fallback;
}

Ray editorRay(const runtime::CameraProjection3D &projection,
              runtime::Vec2 position) {
  const auto ray = projection.ray(position);
  return {ray.origin, normalizedOr(ray.direction, projection.forward)};
}

struct RayBoxInterval {
  float entry;
  float exit;
};

std::optional<RayBoxInterval> rayBox(const Ray &ray,
                                     const runtime::Vec3 minimum,
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
  return RayBoxInterval{nearDistance, farDistance};
}

} // namespace

EditorViewportRay sceneViewportRay(const EditorSceneViewCamera &camera,
                                   const runtime::Vec2 viewportPosition,
                                   const runtime::Vec2 viewportSize) {
  const auto projection = editorProjection(camera, viewportSize);
  return projection
             ? editorRay(*projection, viewportPosition)
             : Ray{camera.position, normalizedOr(camera.forward, {0, 0, 1})};
}

std::optional<runtime::Vec2>
projectScenePoint3D(const EditorSceneViewCamera &camera,
                    const runtime::Vec3 worldPoint,
                    const runtime::Vec2 viewportSize) {
  const auto projection = editorProjection(camera, viewportSize);
  const auto point =
      projection ? projection->project(worldPoint) : std::nullopt;
  return point ? std::optional(point->screen) : std::nullopt;
}

runtime::Vec2 projectSceneDirection3D(const EditorSceneViewCamera &camera,
                                      const runtime::Vec3 worldDirection) {
  const auto projection = editorProjection(camera, {1, 1});
  return projection ? runtime::Vec2{dot(worldDirection, projection->right),
                                    -dot(worldDirection, projection->up)}
                    : runtime::Vec2{};
}

float sceneWorldUnitsPerPixel(const EditorSceneViewCamera &camera,
                              runtime::Vec3 worldPoint,
                              runtime::Vec2 viewportSize) {
  if (viewportSize.y <= 0) {
    return 0;
  }
  if (!camera.projection.perspective) {
    // Gizmo scaling retains its smaller minimum than point/ray projection.
    return std::isfinite(camera.projection.orthographicSize)
               ? std::max(camera.projection.orthographicSize, 0.01F) /
                     viewportSize.y
               : 0.0F;
  }
  const auto projection = editorProjection(camera, viewportSize);
  if (!projection) {
    return 0;
  }
  const float depth = std::max(
      dot(subtract(worldPoint, camera.position), projection->forward), 0.0F);
  return 1.0F / projection->pixelsPerUnit(depth);
}

std::optional<runtime::Vec3>
intersectSceneGroundPlane3D(const EditorSceneViewCamera &camera,
                            const runtime::Vec2 viewportPosition,
                            const runtime::Vec2 viewportSize) {
  if (viewportSize.x <= 0.0F || viewportSize.y <= 0.0F)
    return std::nullopt;
  const Ray ray = sceneViewportRay(camera, viewportPosition, viewportSize);
  if (std::abs(ray.direction.y) <= 0.000001F)
    return std::nullopt;
  const float distance = -ray.origin.y / ray.direction.y;
  if (distance < 0.0F)
    return std::nullopt;
  return add(ray.origin, scale(ray.direction, distance));
}

std::optional<std::string> pickSceneEntity3D(
    const runtime::World &world, const EditorSceneViewCamera &camera,
    const runtime::Vec2 viewportPosition, const runtime::Vec2 viewportSize) {
  const auto projection = editorProjection(camera, viewportSize);
  if (!projection) {
    return std::nullopt;
  }
  const Ray ray = editorRay(*projection, viewportPosition);
  const float viewDepthPerUnit = dot(ray.direction, projection->forward);
  if (viewDepthPerUnit <= 0)
    return std::nullopt;
  const float nearDistance =
      std::max(camera.projection.nearClip, 0.0F) / viewDepthPerUnit;
  float nearestDistance =
      std::max(camera.projection.farClip, 0.0F) / viewDepthPerUnit;
  std::optional<std::string> nearest;

  for (const runtime::Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    const auto bounds = editorEntityBounds3D(world, entity);
    if (!bounds)
      continue;
    const auto considerTransform = [&](const runtime::WorldTransform3D &pose) {
      // Camera/light/empty markers behind the near plane are not drawn. Their
      // proxy bounds must not steal clicks while the camera is inside them.
      if (!bounds->isSceneGeometry && !projection->project(pose.position))
        return;
      const Ray local{
          .origin = runtime::inverseTransformPoint3D(pose, ray.origin),
          .direction = runtime::inverseTransformVector3D(pose, ray.direction)};
      const auto interval =
          rayBox(local, bounds->local.minimum, bounds->local.maximum);
      if (!interval || interval->exit < nearDistance)
        return;
      const float distance = std::max(interval->entry, nearDistance);
      if (distance < nearestDistance) {
        nearestDistance = distance;
        const auto terrainOwner = runtime::terrainSurfaceOwner(entity);
        nearest = terrainOwner ? std::string(*terrainOwner) : entity.id;
      }
    };

    for (const runtime::WorldTransform3D &transform : bounds->worldTransforms)
      considerTransform(transform);
  }
  return nearest;
}

} // namespace demi::editor
