#include "demi/runtime/animation/ProceduralIk.h"
#include "demi/runtime/math/VectorMath.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime {
namespace {

constexpr float Epsilon = 0.000001F;

bool finite(const Vec2 value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(const Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

Vec3 perpendicularTo(const Vec3 direction, const Vec3 poleOffset) {
  Vec3 bend = math::subtract(
      poleOffset, math::scale(direction, math::dot(poleOffset, direction)));
  if (math::dot(bend, bend) <= Epsilon * Epsilon) {
    const Vec3 axis =
        std::abs(direction.y) < 0.9F ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    bend = math::subtract(axis,
                          math::scale(direction, math::dot(axis, direction)));
  }
  return math::normalized(bend);
}

bool validLengths(const float upperLength, const float lowerLength) {
  return std::isfinite(upperLength) && std::isfinite(lowerLength) &&
         upperLength > 0.0F && lowerLength > 0.0F;
}

} // namespace

std::optional<TwoBoneIkResult2D>
solveTwoBoneIk2D(const Vec2 root, const Vec2 target, const Vec2 pole,
                 const float upperLength, const float lowerLength) {
  if (!finite(root) || !finite(target) || !finite(pole) ||
      !validLengths(upperLength, lowerLength))
    return std::nullopt;

  const Vec2 offset{target.x - root.x, target.y - root.y};
  const float targetDistance =
      std::sqrt(offset.x * offset.x + offset.y * offset.y);
  const Vec2 direction =
      targetDistance > Epsilon
          ? Vec2{offset.x / targetDistance, offset.y / targetDistance}
          : Vec2{1.0F, 0.0F};
  Vec2 bend{-direction.y, direction.x};
  const Vec2 poleOffset{pole.x - root.x, pole.y - root.y};
  if (poleOffset.x * bend.x + poleOffset.y * bend.y < 0.0F)
    bend = {-bend.x, -bend.y};

  const float minimum = std::abs(upperLength - lowerLength);
  const float maximum = upperLength + lowerLength;
  const float distance = std::clamp(targetDistance, minimum, maximum);
  const bool reached = targetDistance >= minimum - Epsilon &&
                       targetDistance <= maximum + Epsilon;
  const Vec2 end = reached ? target
                           : Vec2{root.x + direction.x * distance,
                                  root.y + direction.y * distance};
  const float along = distance > Epsilon
                          ? (upperLength * upperLength + distance * distance -
                             lowerLength * lowerLength) /
                                (2.0F * distance)
                          : 0.0F;
  const float height =
      std::sqrt(std::max(upperLength * upperLength - along * along, 0.0F));
  return TwoBoneIkResult2D{
      .joint = {root.x + direction.x * along + bend.x * height,
                root.y + direction.y * along + bend.y * height},
      .end = end,
      .reached = reached,
  };
}

std::optional<TwoBoneIkResult3D>
solveTwoBoneIk3D(const Vec3 root, const Vec3 target, const Vec3 pole,
                 const float upperLength, const float lowerLength) {
  if (!finite(root) || !finite(target) || !finite(pole) ||
      !validLengths(upperLength, lowerLength))
    return std::nullopt;

  const Vec3 offset = math::subtract(target, root);
  const float targetDistance = std::sqrt(math::dot(offset, offset));
  const Vec3 direction = targetDistance > Epsilon
                             ? math::scale(offset, 1.0F / targetDistance)
                             : Vec3{1.0F, 0.0F, 0.0F};
  const Vec3 bend = perpendicularTo(direction, math::subtract(pole, root));
  const float minimum = std::abs(upperLength - lowerLength);
  const float maximum = upperLength + lowerLength;
  const float distance = std::clamp(targetDistance, minimum, maximum);
  const bool reached = targetDistance >= minimum - Epsilon &&
                       targetDistance <= maximum + Epsilon;
  const Vec3 end =
      reached ? target : math::add(root, math::scale(direction, distance));
  const float along = distance > Epsilon
                          ? (upperLength * upperLength + distance * distance -
                             lowerLength * lowerLength) /
                                (2.0F * distance)
                          : 0.0F;
  const float height =
      std::sqrt(std::max(upperLength * upperLength - along * along, 0.0F));
  return TwoBoneIkResult3D{
      .joint = math::add(root, math::add(math::scale(direction, along),
                                         math::scale(bend, height))),
      .end = end,
      .reached = reached,
  };
}

} // namespace demi::runtime
