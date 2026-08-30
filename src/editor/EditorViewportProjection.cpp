#include "editor/EditorViewportProjection.h"

#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/EngineComponents.h"
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

std::optional<std::string> pickSceneEntity3D(
    const runtime::World &world, const EditorSceneViewCamera &camera,
    const runtime::Vec2 viewportPosition, const runtime::Vec2 viewportSize) {
  const Ray ray = cameraRay(camera, viewportPosition, viewportSize);
  float nearestDistance = std::max(camera.projection.farClip, 0.0F);
  std::optional<std::string> nearest;

  for (const runtime::Entity &entity : world.entities) {
    if (!entity.enabled ||
        !entity.hasComponent<runtime::Transform3DComponent>())
      continue;
    const auto transform = runtime::resolveWorldTransform3D(world, entity);
    if (!transform)
      continue;
    runtime::Vec3 minimum{-0.2F, -0.2F, -0.2F};
    runtime::Vec3 maximum{0.2F, 0.2F, 0.2F};
    if (const auto box = runtime::resolvedBoxCollider3D(world, entity)) {
      const runtime::Vec3 half{box->size.x * 0.5F, box->size.y * 0.5F,
                               box->size.z * 0.5F};
      minimum = subtract(box->offset, half);
      maximum = add(box->offset, half);
    } else if (const auto *sphere =
                   entity.component<runtime::SphereCollider3DComponent>()) {
      const runtime::Vec3 radius{sphere->radius, sphere->radius,
                                 sphere->radius};
      minimum = subtract(sphere->offset, radius);
      maximum = add(sphere->offset, radius);
    } else if (const auto *capsule =
                   entity.component<runtime::CapsuleCollider3DComponent>()) {
      const runtime::Vec3 half{capsule->radius, capsule->height * 0.5F,
                               capsule->radius};
      minimum = subtract(capsule->offset, half);
      maximum = add(capsule->offset, half);
    } else if (const auto *convex =
                   entity.component<runtime::ConvexCollider3DComponent>();
               convex != nullptr && !convex->points.empty()) {
      minimum = add(convex->points.front(), convex->offset);
      maximum = minimum;
      for (const runtime::Vec3 point : convex->points) {
        const runtime::Vec3 value = add(point, convex->offset);
        minimum = {std::min(minimum.x, value.x), std::min(minimum.y, value.y),
                   std::min(minimum.z, value.z)};
        maximum = {std::max(maximum.x, value.x), std::max(maximum.y, value.y),
                   std::max(maximum.z, value.z)};
      }
    } else if (const auto *mesh =
                   entity.component<runtime::MeshRendererComponent>()) {
      minimum = mesh->hasBounds ? mesh->boundsMin
                                : runtime::Vec3{-0.5F, -0.5F, -0.5F};
      maximum =
          mesh->hasBounds ? mesh->boundsMax : runtime::Vec3{0.5F, 0.5F, 0.5F};
      minimum = {minimum.x * mesh->size.x, minimum.y * mesh->size.y,
                 minimum.z * mesh->size.z};
      maximum = {maximum.x * mesh->size.x, maximum.y * mesh->size.y,
                 maximum.z * mesh->size.z};
      if (minimum.x > maximum.x)
        std::swap(minimum.x, maximum.x);
      if (minimum.y > maximum.y)
        std::swap(minimum.y, maximum.y);
      if (minimum.z > maximum.z)
        std::swap(minimum.z, maximum.z);
    }
    const Ray local{
        .origin = runtime::inverseTransformPoint3D(*transform, ray.origin),
        .direction =
            runtime::inverseTransformVector3D(*transform, ray.direction)};
    const auto distance = rayBox(local, minimum, maximum);
    if (distance && *distance > camera.projection.nearClip &&
        *distance < nearestDistance) {
      nearestDistance = *distance;
      nearest = entity.id;
    }
  }
  return nearest;
}

} // namespace demi::editor
