#include "demi/runtime/camera/Camera3DMath.h"

#include "demi/runtime/math/VectorMath.h"
#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace demi::runtime {
namespace {

std::optional<Vec3> normalizedViewDirection(const Vec3 value) {
  const float length = std::hypot(value.x, value.y, value.z);
  if (!std::isfinite(length) || length <= 0.000001F) {
    return std::nullopt;
  }
  return math::scale(value, 1.0F / length);
}

} // namespace

std::optional<CameraProjection3D>
cameraProjection3D(const Camera3DComponent &camera, const Vec3 position,
                   const Vec3 forward, const Vec3 up, const Vec2 viewport) {
  const auto viewForward = normalizedViewDirection(forward);
  if (!viewForward) {
    return std::nullopt;
  }
  const auto viewRight = normalizedViewDirection(math::cross(*viewForward, up));
  if (!viewRight) {
    return std::nullopt;
  }
  const float halfHeight =
      camera.perspective ? std::tan(std::clamp(camera.fov, 1.0F, 179.0F) *
                                    std::numbers::pi_v<float> / 360.0F)
                         : std::max(camera.orthographicSize, 0.001F) * 0.5F;
  if (!std::isfinite(halfHeight)) {
    return std::nullopt;
  }
  return CameraProjection3D{
      position,
      *viewForward,
      *viewRight,
      math::cross(*viewRight, *viewForward),
      {std::max(viewport.x, 1.0F), std::max(viewport.y, 1.0F)},
      halfHeight,
      camera.nearClip,
      camera.farClip,
      camera.perspective};
}

std::optional<CameraProjection3D>
cameraProjection3D(const WorldTransform3D &transform,
                   const Camera3DComponent &camera, const Vec2 viewport) {
  return cameraProjection3D(
      camera, transform.position,
      transformDirection3D(transform, camera.targetOffset),
      transformDirection3D(transform, {0.0F, camera.upAxis, 0.0F}), viewport);
}

float CameraProjection3D::pixelsPerUnit(const float depth) const {
  const float verticalExtent = halfHeight * (perspective ? depth : 1.0F);
  return viewport.y / (2.0F * verticalExtent);
}

std::optional<CameraProjectedPoint3D>
CameraProjection3D::project(const Vec3 world) const {
  const Vec3 offset = math::subtract(world, position);
  const float depth = math::dot(offset, forward);
  if (depth < nearClip || depth > farClip || (perspective && depth <= 0.0F)) {
    return std::nullopt;
  }
  const float scale = pixelsPerUnit(depth);
  return CameraProjectedPoint3D{
      {viewport.x * 0.5F + math::dot(offset, right) * scale,
       viewport.y * 0.5F - math::dot(offset, up) * scale},
      depth,
      scale};
}

CameraRay3D CameraProjection3D::ray(const Vec2 screen) const {
  const float horizontal = (screen.x * 2.0F / viewport.x - 1.0F) * halfHeight *
                           viewport.x / viewport.y;
  const float vertical = (1.0F - screen.y * 2.0F / viewport.y) * halfHeight;
  const Vec3 offset =
      math::add(math::scale(right, horizontal), math::scale(up, vertical));
  if (!perspective) {
    return {math::add(position, offset), forward};
  }
  return {position, math::normalized(math::add(forward, offset))};
}

CameraRay3D cameraScreenRay3D(const WorldTransform3D &transform,
                              const Camera3DComponent &camera,
                              const Vec2 screen, const Vec2 viewport) {
  const auto projection = cameraProjection3D(transform, camera, viewport);
  return projection ? projection->ray(screen)
                    : CameraRay3D{transform.position, {}};
}

std::optional<Vec2> worldToScreen3D(const WorldTransform3D &transform,
                                    const Camera3DComponent &camera,
                                    const Vec3 world, const Vec2 viewport) {
  const auto projection = cameraProjection3D(transform, camera, viewport);
  const auto point = projection ? projection->project(world) : std::nullopt;
  return point ? std::optional<Vec2>(point->screen) : std::nullopt;
}

Vec3 screenToWorld3D(const WorldTransform3D &transform,
                     const Camera3DComponent &camera, const Vec2 screen,
                     const Vec2 viewport, const float distance) {
  const CameraRay3D ray =
      cameraScreenRay3D(transform, camera, screen, viewport);
  return math::add(ray.origin,
                   math::scale(ray.direction, std::max(distance, 0.0F)));
}

} // namespace demi::runtime
