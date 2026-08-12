#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"

#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <cstdint>

namespace demi::runtime::render {
namespace {

float dot(const Vec3 left, const Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 cross(const Vec3 left, const Vec3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

Vec3 normalized(const Vec3 value, const Vec3 fallback) {
  const float lengthSquared = dot(value, value);
  if (lengthSquared <= 0.000001F)
    return fallback;
  const float inverseLength = 1.0F / std::sqrt(lengthSquared);
  return {value.x * inverseLength, value.y * inverseLength,
          value.z * inverseLength};
}

struct Sphere {
  Vec3 center;
  float radius = 0.0F;
};

Sphere worldBounds(const MeshRendererComponent &mesh,
                   WorldTransform3D transform) {
  transform.scale = {transform.scale.x * mesh.size.x,
                     transform.scale.y * mesh.size.y,
                     transform.scale.z * mesh.size.z};
  Vec3 minimum;
  Vec3 maximum;
  bool first = true;
  for (const float x : {mesh.boundsMin.x, mesh.boundsMax.x})
    for (const float y : {mesh.boundsMin.y, mesh.boundsMax.y})
      for (const float z : {mesh.boundsMin.z, mesh.boundsMax.z}) {
        const Vec3 point = transformPoint3D(transform, {x, y, z});
        if (first) {
          minimum = maximum = point;
          first = false;
        } else {
          minimum = {std::min(minimum.x, point.x),
                     std::min(minimum.y, point.y),
                     std::min(minimum.z, point.z)};
          maximum = {std::max(maximum.x, point.x),
                     std::max(maximum.y, point.y),
                     std::max(maximum.z, point.z)};
        }
      }
  const Vec3 center{(minimum.x + maximum.x) * 0.5F,
                    (minimum.y + maximum.y) * 0.5F,
                    (minimum.z + maximum.z) * 0.5F};
  const Vec3 extent{maximum.x - center.x, maximum.y - center.y,
                    maximum.z - center.z};
  return {.center = center, .radius = std::sqrt(dot(extent, extent))};
}

bool visible(const Sphere sphere, const BgfxCameraFrame3D &frame) {
  const Vec3 forward = normalized(frame.forward, {0.0F, 0.0F, 1.0F});
  const Vec3 right =
      normalized(cross(frame.up, forward), {1.0F, 0.0F, 0.0F});
  const Vec3 up = normalized(cross(forward, right), {0.0F, 1.0F, 0.0F});
  const Vec3 relative{sphere.center.x - frame.position.x,
                      sphere.center.y - frame.position.y,
                      sphere.center.z - frame.position.z};
  const float depth = dot(relative, forward);
  if (depth + sphere.radius < frame.camera.nearClip ||
      depth - sphere.radius > frame.camera.farClip)
    return false;

  const float aspect = static_cast<float>(std::max(frame.viewportWidth,
                                                    std::uint16_t{1})) /
                       static_cast<float>(std::max(frame.viewportHeight,
                                                    std::uint16_t{1}));
  float halfWidth = std::max(frame.camera.orthographicSize, 0.001F) * aspect;
  float halfHeight = std::max(frame.camera.orthographicSize, 0.001F);
  float horizontalRadius = sphere.radius;
  float verticalRadius = sphere.radius;
  if (frame.camera.perspective) {
    constexpr float DegreesToRadians = 0.01745329251994329577F;
    const float tangent =
        std::tan(std::clamp(frame.camera.fov, 1.0F, 179.0F) *
                 DegreesToRadians * 0.5F);
    halfHeight = std::max(depth, 0.0F) * tangent;
    halfWidth = halfHeight * aspect;
    verticalRadius *= std::sqrt(1.0F + tangent * tangent);
    const float horizontalTangent = tangent * aspect;
    horizontalRadius *=
        std::sqrt(1.0F + horizontalTangent * horizontalTangent);
  }
  return std::abs(dot(relative, right)) <= halfWidth + horizontalRadius &&
         std::abs(dot(relative, up)) <= halfHeight + verticalRadius;
}

} // namespace

SceneVisibility3D extractVisibleMeshes3D(const World &world,
                                         const BgfxCameraFrame3D &frame,
                                         JobSystem *jobs) {
  std::vector<std::optional<VisibleMesh3D>> extracted(world.entities.size());
  // byte-per-entry storage lets workers write independent indices without the
  // packed-bit races of std::vector<bool>.
  std::vector<std::uint8_t> considered(world.entities.size(), 0);
  std::vector<std::uint8_t> culled(world.entities.size(), 0);
  const auto extract = [&](const std::size_t index) {
    const Entity &entity = world.entities[index];
    if (!entity.enabled)
      return;
    const auto *mesh = entity.component<MeshRendererComponent>();
    if (mesh == nullptr)
      return;
    if (!frame.camera.renderMask.empty() && !mesh->renderLayer.empty() &&
        frame.camera.renderMask != mesh->renderLayer)
      return;
    considered[index] = 1;
    const auto transform = resolveWorldTransform3D(world, entity);
    if (!transform)
      return;
    if (mesh->hasBounds && !visible(worldBounds(*mesh, *transform), frame)) {
      culled[index] = 1;
      return;
    }
    extracted[index] = VisibleMesh3D{.entity = &entity,
                                     .transform = *transform};
  };
  // Transform hierarchy lookup is cheap for ordinary scenes. Dispatch only
  // when extraction is large enough to amortize wake-up and synchronization.
  constexpr std::size_t ParallelExtractionThreshold = 1024;
  if (jobs != nullptr && world.entities.size() >= ParallelExtractionThreshold)
    jobs->parallelFor(world.entities.size(), 64, extract);
  else
    for (std::size_t index = 0; index < world.entities.size(); ++index)
      extract(index);

  SceneVisibility3D result;
  result.meshes.reserve(world.entities.size());
  for (std::size_t index = 0; index < extracted.size(); ++index) {
    result.considered += considered[index] ? 1U : 0U;
    result.culled += culled[index] ? 1U : 0U;
    if (extracted[index])
      result.meshes.push_back(std::move(*extracted[index]));
  }
  return result;
}

} // namespace demi::runtime::render
