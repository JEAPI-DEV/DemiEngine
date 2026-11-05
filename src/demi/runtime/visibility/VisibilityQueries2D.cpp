#include "demi/runtime/visibility/VisibilityQueries2D.h"
#include "demi/runtime/physics/RadialRaycast2D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace demi::runtime {
VisibilityBoundary2D
castVisibilityBoundary2D(const World &world, Vec2 focus, float radius,
                         const std::string &occluderLayer) {
  ProfileScope queryScope("Visibility.rays2d");
  if (!std::isfinite(radius) || radius <= 0 ||
      !std::isfinite(focus.x) || !std::isfinite(focus.y)) {
    return {};
  }
  constexpr float tau = 2.0F * std::numbers::pi_v<float>;
  // Bound the inscribed range-circle chord error independently of radius.
  // At the maximum supported radius this matches the old 512-ray accuracy;
  // smaller sight ranges do not need the same number of uniform rays.
  const double rangeChordError = 256.0 *
      (1.0 - std::cos(std::numbers::pi_v<double> / 512.0));
  const double halfStep = std::acos(1.0 - std::min(rangeChordError / radius, 1.0));
  const double requestedRayCount = halfStep > 0
      ? std::ceil(std::numbers::pi_v<double> / halfStep)
      : 512.0;
  // Native callers may use larger finite ranges than the Lua maximum of 256.
  // Keep the prior uniform-ray ceiling there and clamp before integer conversion.
  const int baseRayCount = static_cast<int>(
      std::clamp(requestedRayCount, 32.0, 512.0));
  std::size_t raysCast = 0;
  const RadialRaycast2D query(world, focus, radius, occluderLayer);
  const auto cast = [&](float angle) {
    ++raysCast;
    return query.cast({std::cos(angle), std::sin(angle)});
  };
  const auto sameSurface = [](const auto &a, const auto &b) {
    if (!a || !b) {
      return a.has_value() == b.has_value();
    }
    const float normalDelta = std::abs(a->normal.x - b->normal.x) +
                              std::abs(a->normal.y - b->normal.y);
    const float planeDelta = std::abs(a->normal.x * (a->point.x - b->point.x) +
                                      a->normal.y * (a->point.y - b->point.y));
    const bool coplanar = normalDelta < 0.001F && planeDelta < 0.001F;
    const bool smoothSurface =
        a->entityId == b->entityId && normalDelta < 0.1F && planeDelta < 0.01F;
    return coplanar || smoothSurface;
  };
  const auto corners = query.silhouetteAngles();
  std::vector<float> samples = corners;
  samples.reserve(corners.size() * 3 + baseRayCount);
  for (int index = 0; index < baseRayCount; ++index) {
    samples.push_back(index * tau / baseRayCount);
  }
  for (const float angle : corners) {
    samples.push_back(angle == 0 ? std::nextafter(tau, 0.0F)
                                 : std::nextafter(angle, 0.0F));
    const float after = std::nextafter(angle, tau);
    samples.push_back(after < tau ? after : 0.0F);
  }
  std::ranges::sort(samples);
  samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
  std::vector<std::optional<PhysicsRaycastHit2D>> hits;
  hits.reserve(samples.size());
  for (const float angle : samples) {
    hits.push_back(cast(angle));
  }
  std::vector<float> distances;
  std::vector<float> angles;
  distances.reserve(samples.size() + 128);
  angles.reserve(samples.size() + 128);
  const auto append = [&](float angle, const auto &hit) {
    if (angle < tau && (angles.empty() || angle > angles.back())) {
      angles.push_back(angle);
      distances.push_back(hit ? hit->distance : radius);
    }
  };
  const auto refine = [&](auto &&self, float leftAngle, const auto &leftHit,
                           float rightAngle, const auto &rightHit,
                           int depth) -> void {
    const bool continuous = sameSurface(leftHit, rightHit);
    const bool curved = leftHit && rightHit &&
        leftHit->entityId == rightHit->entityId &&
        (std::abs(leftHit->normal.x - rightHit->normal.x) +
         std::abs(leftHit->normal.y - rightHit->normal.y)) > 0.00001F;
    if (continuous && !curved) {
      return;
    }
    const float middleAngle = (leftAngle + rightAngle) * 0.5F;
    if (depth == 40 || middleAngle <= leftAngle || middleAngle >= rightAngle) {
      append(leftAngle, leftHit);
      append(rightAngle, rightHit);
      return;
    }
    const auto middleHit = cast(middleAngle);
    if (continuous && curved && middleHit &&
        middleHit->entityId == leftHit->entityId) {
      const Vec2 a{leftHit->distance * std::cos(leftAngle),
                    leftHit->distance * std::sin(leftAngle)};
      const Vec2 b{rightHit->distance * std::cos(rightAngle),
                    rightHit->distance * std::sin(rightAngle)};
      const Vec2 direction{std::cos(middleAngle), std::sin(middleAngle)};
      const float determinant = direction.x * (b.y - a.y) -
                                  direction.y * (b.x - a.x);
      if (std::abs(determinant) > 0.00000001F) {
        const float chordDistance = (a.x * b.y - a.y * b.x) / determinant;
        if (std::abs(chordDistance - middleHit->distance) <= 0.001F) {
          return;
        }
      }
    }
    // A bin can cross several wall faces. Refine both differing halves rather
    // than discarding a third surface encountered while bracketing one corner.
    self(self, leftAngle, leftHit, middleAngle, middleHit, depth + 1);
    if (curved) {
      append(middleAngle, middleHit);
    }
    self(self, middleAngle, middleHit, rightAngle, rightHit, depth + 1);
  };
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const float leftAngle = samples[index];
    const float rightAngle = index + 1 < samples.size() ? samples[index + 1] : tau;
    const auto &leftHit = hits[index];
    const auto &rightHit = hits[(index + 1) % samples.size()];
    append(leftAngle, leftHit);
    refine(refine, leftAngle, leftHit, rightAngle, rightHit, 0);
  }
  RuntimeProfiler::setGauge("Visibility.rays_cast", static_cast<double>(raysCast));
  RuntimeProfiler::setGauge("Visibility.boundary_points", static_cast<double>(angles.size()));
  return {std::move(distances), std::move(angles)};
}
} // namespace demi::runtime
