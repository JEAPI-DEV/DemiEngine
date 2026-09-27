#include "demi/runtime/render/bgfx3d/ShadowCascadeLayout3D.h"
#include "demi/runtime/math/VectorMath.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace demi::runtime::render {
namespace {
bool finite(Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}
} // namespace

std::optional<ShadowCascadeLayout3D>
makeShadowCascadeLayout3D(const SceneLighting3D &light,
                          const BgfxCameraFrame3D &camera, std::string &error) {
  using namespace math;
  const Vec3 direction = normalized(
      Vec3{light.direction[0], light.direction[1], light.direction[2]});
  const Vec3 forward = normalized(camera.forward);
  if (!finite(direction) || !finite(forward) || !finite(camera.position) ||
      length(direction) < 0.5F || length(forward) < 0.5F ||
      (light.shadowCascades < 1 || light.shadowCascades > int(MaxShadowCascades3D)) ||
      light.shadowResolution < 1 || !std::isfinite(light.shadowDistance) ||
      !std::isfinite(light.shadowSplitLambda) || light.shadowSplitLambda < 0 ||
      light.shadowSplitLambda > 1 || !std::isfinite(light.shadowBlend) ||
      light.shadowBlend < 0 || light.shadowBlend > 1.F ||
      !std::isfinite(camera.camera.nearClip) ||
      !std::isfinite(camera.camera.farClip) ||
      !std::isfinite(camera.camera.fov) ||
      !std::isfinite(camera.camera.orthographicSize) ||
      camera.camera.nearClip <= 0 ||
      camera.camera.farClip <= camera.camera.nearClip ||
      camera.camera.fov <= 0 || camera.camera.fov >= 179 ||
      camera.camera.orthographicSize <= 0 || camera.viewportWidth == 0 ||
      camera.viewportHeight == 0) {
    error = "Invalid directional shadow or camera projection settings";
    return std::nullopt;
  }
  const float nearDepth = camera.camera.nearClip;
  const float farDepth = std::min(light.shadowDistance, camera.camera.farClip);
  ShadowCascadeLayout3D layout;
  if (farDepth <= nearDepth)
    return layout;
  layout.count = light.shadowCascades;
  layout.cameraDepth = {forward.x, forward.y, forward.z,
                        -dot(forward, camera.position)};
  const Vec3 right = normalized(cross(direction, std::abs(direction.y) > 0.99F
                                                     ? Vec3{0, 0, 1}
                                                     : Vec3{0, 1, 0}));
  const Vec3 up = cross(right, direction);
  const float aspect =
      static_cast<float>(camera.viewportWidth) / camera.viewportHeight;
  const float tangent =
      std::tan(camera.camera.fov * std::numbers::pi_v<float> / 360.F);
  float previous = nearDepth;
  for (int index = 0; index < layout.count; ++index) {
    const float fraction = static_cast<float>(index + 1) / layout.count;
    const float uniform = nearDepth + (farDepth - nearDepth) * fraction;
    const float logarithmic =
        nearDepth * std::pow(farDepth / nearDepth, fraction);
    const float split =
        index + 1 == layout.count
            ? farDepth
            : std::lerp(uniform, logarithmic,
                        camera.camera.perspective ? light.shadowSplitLambda
                                                  : 0.F);
    // The preceding cascade blends into this one before its nominal end.
    const float precedingStart =
        index > 1 ? layout.splits[index - 2] : nearDepth;
    const float start =
        index == 0 ? nearDepth
                   : previous - (previous - precedingStart) * light.shadowBlend;
    const float midpoint = (start + split) * 0.5F;
    Vec3 center = add(camera.position, scale(forward, midpoint));
    const float halfHeight = camera.camera.perspective
                                 ? split * tangent
                                 : camera.camera.orthographicSize * 0.5F;
    const float halfWidth = halfHeight * aspect;
    const float halfLength = (split - start) * 0.5F;
    float radius = std::sqrt(halfWidth * halfWidth + halfHeight * halfHeight +
                             halfLength * halfLength);
    radius = std::ceil(radius * 16.F) / 16.F;
    radius *= 1.F + 4.F / light.shadowResolution;
    const float texel = 2.F * radius / light.shadowResolution;
    for (Vec3 axis : {right, up}) {
      const float coordinate = dot(center, axis);
      center = add(center, scale(axis, std::round(coordinate / texel) * texel -
                                           coordinate));
    }
    auto &cascade = layout.cascades[index];
    cascade.radius = radius;
    cascade.depthRange = 2.F * (radius + light.shadowDistance);
    if (!std::isfinite(cascade.depthRange) || !std::isfinite(1.F / radius)) {
      error = "Directional shadow coverage exceeds finite projection range";
      return std::nullopt;
    }
    cascade.eye = subtract(center, scale(direction, cascade.depthRange * 0.5F));
    cascade.direction = direction;
    cascade.up = up;
    cascade.receiverNear = start;
    cascade.receiverFar = split;
    cascade.x = {right.x / (2 * radius), right.y / (2 * radius),
                 right.z / (2 * radius),
                 0.5F - dot(right, cascade.eye) / (2 * radius)};
    cascade.y = {up.x / (2 * radius), up.y / (2 * radius), up.z / (2 * radius),
                 0.5F - dot(up, cascade.eye) / (2 * radius)};
    cascade.z = {direction.x / cascade.depthRange,
                 direction.y / cascade.depthRange,
                 direction.z / cascade.depthRange,
                 -dot(direction, cascade.eye) / cascade.depthRange};
    layout.splits[index] = split;
    previous = split;
  }
  return layout;
}
} // namespace demi::runtime::render
