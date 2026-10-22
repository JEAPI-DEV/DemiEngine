#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace demi::runtime::render {

struct BgfxCameraFrame3D;

// Camera-relative side planes of a receiver slice extruded along the light.
// This only tightens caster culling; shadow projection/depth bounds are
// separate.
struct ShadowCasterHull3D {
  std::array<float, 3> origin{};
  std::array<std::array<float, 4>, 8> planes{};
  std::uint8_t count = 0;

  bool operator==(const ShadowCasterHull3D &) const = default;

  [[nodiscard]] bool intersectsSphere(Vec3 center, float radius) const {
    for (std::size_t index = 0; index < count; ++index) {
      const auto &plane = planes[index];
      // Subtract the origin before projecting, with double intermediates to
      // avoid subtracting two large absolute plane distances.
      const double distance = plane[0] * (double(center.x) - origin[0]) +
                              plane[1] * (double(center.y) - origin[1]) +
                              plane[2] * (double(center.z) - origin[2]);
      if (distance >
          double(plane[3]) +
              radius * (1.0 + 8 * std::numeric_limits<float>::epsilon())) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool intersectsAabb(Vec3 minimum, Vec3 maximum,
                                    float expansion) const {
    for (std::size_t index = 0; index < count; ++index) {
      const auto &plane = planes[index];
      // Minimum support lies outside an outward plane only when the entire
      // expanded node is outside. Geometry/wind expansion is world-space.
      const double support =
          plane[0] *
              (double(plane[0] >= 0 ? minimum.x : maximum.x) - origin[0]) +
          plane[1] *
              (double(plane[1] >= 0 ? minimum.y : maximum.y) - origin[1]) +
          plane[2] *
              (double(plane[2] >= 0 ? minimum.z : maximum.z) - origin[2]);
      if (support >
          double(plane[3]) +
              expansion * (1.0 + 8 * std::numeric_limits<float>::epsilon())) {
        return false;
      }
    }
    return true;
  }
};

[[nodiscard]] ShadowCasterHull3D
makeShadowCasterHull3D(const BgfxCameraFrame3D &camera, float receiverNear,
                       float receiverFar, Vec3 lightRight, Vec3 lightUp,
                       float worldTexel, float casterDepthRange,
                       bool hardFilter);

} // namespace demi::runtime::render
