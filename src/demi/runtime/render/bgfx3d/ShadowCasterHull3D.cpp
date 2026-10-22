#include "demi/runtime/render/bgfx3d/ShadowCasterHull3D.h"

#include "demi/runtime/math/VectorMath.h"
#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"

#include <algorithm>
#include <limits>
#include <numbers>

namespace demi::runtime::render {
namespace {
struct Point {
  double x = 0;
  double y = 0;
  bool operator==(const Point &) const = default;
};

double orientation(Point a, Point b, Point c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

double projected(Vec3 axis, Vec3 forward, Vec3 right, Vec3 up, double depth,
                 double horizontal, double vertical) {
  return double(axis.x) *
             (forward.x * depth + right.x * horizontal + up.x * vertical) +
         double(axis.y) *
             (forward.y * depth + right.y * horizontal + up.y * vertical) +
         double(axis.z) *
             (forward.z * depth + right.z * horizontal + up.z * vertical);
}
} // namespace

ShadowCasterHull3D makeShadowCasterHull3D(const BgfxCameraFrame3D &camera,
                                          float receiverNear, float receiverFar,
                                          Vec3 lightRight, Vec3 lightUp,
                                          float worldTexel,
                                          float casterDepthRange,
                                          bool hardFilter) {
  using namespace math;
  ShadowCasterHull3D result;
  result.origin = {camera.position.x, camera.position.y, camera.position.z};
  const auto forward = normalized(camera.forward);
  const auto right = normalized(cross(camera.up, forward));
  const auto up = normalized(cross(forward, right));
  if (length(right) < .5F || length(up) < .5F) {
    // An undefined camera basis cannot justify rejecting a caster.
    return result;
  }
  const double aspect = double(camera.viewportWidth) / camera.viewportHeight;
  const double tangent =
      std::tan(double(camera.camera.fov) * std::numbers::pi / 360.0);
  std::array<Point, 8> points;
  std::size_t used = 0;
  for (const double depth : {double(receiverNear), double(receiverFar)}) {
    const double halfHeight = camera.camera.perspective
                                  ? depth * tangent
                                  : double(camera.camera.orthographicSize) * .5;
    for (const double horizontal :
         {-halfHeight * aspect, halfHeight * aspect}) {
      for (const double vertical : {-halfHeight, halfHeight}) {
        auto &point = points[used++];
        point = {projected(lightRight, forward, right, up, depth, horizontal,
                           vertical),
                 projected(lightUp, forward, right, up, depth, horizontal,
                           vertical)};
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
          return result;
        }
      }
    }
  }
  std::sort(points.begin(), points.end(), [](Point left, Point right) {
    return left.x != right.x ? left.x < right.x : left.y < right.y;
  });
  const auto end = std::unique(points.begin(), points.end());
  const auto count = std::size_t(end - points.begin());
  if (count < 3) {
    return result;
  }
  // Monotone chain over at most eight points; intermediate endpoints can
  // repeat, hence the doubled fixed workspace. No per-cascade heap state.
  std::array<Point, 16> hull;
  std::size_t hullSize = 0;
  for (std::size_t index = 0; index < count; ++index) {
    while (hullSize >= 2 && orientation(hull[hullSize - 2], hull[hullSize - 1],
                                        points[index]) <= 0) {
      --hullSize;
    }
    hull[hullSize++] = points[index];
  }
  const auto upperStart = hullSize + 1;
  for (std::size_t index = count - 1; index > 0; --index) {
    const auto point = points[index - 1];
    while (hullSize >= upperStart &&
           orientation(hull[hullSize - 2], hull[hullSize - 1], point) <= 0) {
      --hullSize;
    }
    hull[hullSize++] = point;
  }
  --hullSize;
  if (hullSize < 3 || hullSize > result.planes.size()) {
    return result;
  }
  for (std::size_t index = 0; index < hullSize; ++index) {
    const auto a = hull[index];
    const auto b = hull[(index + 1) % hullSize];
    const double edgeLength = std::hypot(b.x - a.x, b.y - a.y);
    const double x = (b.y - a.y) / edgeLength;
    const double y = (a.x - b.x) / edgeLength;
    // Continuous bilinear PCF reaches two texel centres on either axis.
    // Receiver normal offsets add at most 1.5 world texels in any direction.
    const double tapRadius = hardFilter ? .5 : 2.0;
    const double normalRadius = hardFilter ? .5 : 1.5;
    const double filterPadding =
        worldTexel * (tapRadius * (std::abs(x) + std::abs(y)) + normalRadius);
    const double distance = x * a.x + y * a.y;
    // Plane rounding must remain conservative over the existing caster depth
    // range. Absolute-coordinate allowance covers float world-transform input
    // quantization; evaluation itself stays relative to the camera origin.
    const double tolerance =
        64 * std::numeric_limits<float>::epsilon() *
            std::max({1.0, double(casterDepthRange), std::abs(a.x),
                      std::abs(a.y), std::abs(b.x), std::abs(b.y)}) +
        4 * std::numeric_limits<float>::epsilon() *
            std::max({std::abs(double(camera.position.x)),
                      std::abs(double(camera.position.y)),
                      std::abs(double(camera.position.z))});
    result.planes[index] = {float(x * lightRight.x + y * lightUp.x),
                            float(x * lightRight.y + y * lightUp.y),
                            float(x * lightRight.z + y * lightUp.z),
                            float(distance + filterPadding + tolerance)};
  }
  result.count = static_cast<std::uint8_t>(hullSize);
  return result;
}

} // namespace demi::runtime::render
