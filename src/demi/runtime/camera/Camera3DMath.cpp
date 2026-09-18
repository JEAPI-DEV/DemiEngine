#include "demi/runtime/camera/Camera3DMath.h"

#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime {
namespace {

float dot(const Vec3 left, const Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 normalized(const Vec3 value) {
  const float length =
      std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  return length > 0.000001F
             ? Vec3{value.x / length, value.y / length, value.z / length}
             : Vec3{0.0F, 0.0F, 1.0F};
}

float radians(const float degrees) {
  return degrees * 0.01745329251994329577F;
}

Vec3 cross(const Vec3 left, const Vec3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

struct CameraBasis {
  Vec3 forward;
  Vec3 right;
  Vec3 up;
};

CameraBasis cameraBasis(const WorldTransform3D &transform,
                        const Camera3DComponent &camera) {
  // Match the renderer's right-handed look-at view, not the entity's +Z axis.
  const Vec3 forward =
      normalized(transformDirection3D(transform, camera.targetOffset));
  const Vec3 authoredUp =
      transformDirection3D(transform, {0.0F, camera.upAxis, 0.0F});
  const Vec3 right = normalized(cross(forward, authoredUp));
  return {forward, right, normalized(cross(right, forward))};
}

} // namespace

CameraRay3D cameraScreenRay3D(const WorldTransform3D &transform,
                              const Camera3DComponent &camera,
                              const Vec2 screen, const Vec2 viewport) {
  const float width = std::max(viewport.x, 1.0F);
  const float height = std::max(viewport.y, 1.0F);
  const float ndcX = screen.x * 2.0F / width - 1.0F;
  const float ndcY = 1.0F - screen.y * 2.0F / height;
  const auto [forward, right, up] = cameraBasis(transform, camera);

  if (!camera.perspective) {
    const float halfHeight = std::max(camera.orthographicSize, 0.001F) * 0.5F;
    const float halfWidth = halfHeight * width / height;
    return {
        .origin = {transform.position.x + right.x * ndcX * halfWidth +
                       up.x * ndcY * halfHeight,
                   transform.position.y + right.y * ndcX * halfWidth +
                       up.y * ndcY * halfHeight,
                   transform.position.z + right.z * ndcX * halfWidth +
                       up.z * ndcY * halfHeight},
        .direction = forward,
    };
  }

  const float tangent =
      std::tan(radians(std::clamp(camera.fov, 1.0F, 179.0F)) * 0.5F);
  const float horizontal = ndcX * tangent * width / height;
  const float vertical = ndcY * tangent;
  return {
      .origin = transform.position,
      .direction =
          normalized({forward.x + right.x * horizontal + up.x * vertical,
                      forward.y + right.y * horizontal + up.y * vertical,
                      forward.z + right.z * horizontal + up.z * vertical}),
  };
}

std::optional<Vec2> worldToScreen3D(const WorldTransform3D &transform,
                                    const Camera3DComponent &camera,
                                    const Vec3 world, const Vec2 viewport) {
  const float width = std::max(viewport.x, 1.0F);
  const float height = std::max(viewport.y, 1.0F);
  const Vec3 offset{world.x - transform.position.x,
                    world.y - transform.position.y,
                    world.z - transform.position.z};
  const auto [forward, right, up] = cameraBasis(transform, camera);
  const float depth = dot(offset, forward);
  if (depth < camera.nearClip || depth > camera.farClip)
    return std::nullopt;

  float ndcX = 0.0F;
  float ndcY = 0.0F;
  if (camera.perspective) {
    const float tangent =
        std::tan(radians(std::clamp(camera.fov, 1.0F, 179.0F)) * 0.5F);
    ndcX = dot(offset, right) / (depth * tangent * width / height);
    ndcY = dot(offset, up) / (depth * tangent);
  } else {
    const float halfHeight = std::max(camera.orthographicSize, 0.001F) * 0.5F;
    ndcX = dot(offset, right) / (halfHeight * width / height);
    ndcY = dot(offset, up) / halfHeight;
  }
  return Vec2{(ndcX + 1.0F) * width * 0.5F,
              (1.0F - ndcY) * height * 0.5F};
}

Vec3 screenToWorld3D(const WorldTransform3D &transform,
                     const Camera3DComponent &camera, const Vec2 screen,
                     const Vec2 viewport, const float distance) {
  const CameraRay3D ray =
      cameraScreenRay3D(transform, camera, screen, viewport);
  const float amount = std::max(distance, 0.0F);
  return {ray.origin.x + ray.direction.x * amount,
          ray.origin.y + ray.direction.y * amount,
          ray.origin.z + ray.direction.z * amount};
}

} // namespace demi::runtime
