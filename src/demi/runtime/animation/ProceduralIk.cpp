#include "demi/runtime/animation/ProceduralIk.h"

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

float dot(const Vec3 left, const Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 subtract(const Vec3 left, const Vec3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 scale(const Vec3 value, const float amount) {
  return {value.x * amount, value.y * amount, value.z * amount};
}

Vec3 add(const Vec3 left, const Vec3 right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 normalized(const Vec3 value) {
  const float magnitude = std::sqrt(dot(value, value));
  return magnitude > Epsilon ? scale(value, 1.0F / magnitude) : Vec3{};
}

Vec3 perpendicularTo(const Vec3 direction, const Vec3 poleOffset) {
  Vec3 bend =
      subtract(poleOffset, scale(direction, dot(poleOffset, direction)));
  if (dot(bend, bend) <= Epsilon * Epsilon) {
    const Vec3 axis =
        std::abs(direction.y) < 0.9F ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    bend = subtract(axis, scale(direction, dot(axis, direction)));
  }
  return normalized(bend);
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

  const Vec3 offset = subtract(target, root);
  const float targetDistance = std::sqrt(dot(offset, offset));
  const Vec3 direction = targetDistance > Epsilon
                             ? scale(offset, 1.0F / targetDistance)
                             : Vec3{1.0F, 0.0F, 0.0F};
  const Vec3 bend = perpendicularTo(direction, subtract(pole, root));
  const float minimum = std::abs(upperLength - lowerLength);
  const float maximum = upperLength + lowerLength;
  const float distance = std::clamp(targetDistance, minimum, maximum);
  const bool reached = targetDistance >= minimum - Epsilon &&
                       targetDistance <= maximum + Epsilon;
  const Vec3 end = reached ? target : add(root, scale(direction, distance));
  const float along = distance > Epsilon
                          ? (upperLength * upperLength + distance * distance -
                             lowerLength * lowerLength) /
                                (2.0F * distance)
                          : 0.0F;
  const float height =
      std::sqrt(std::max(upperLength * upperLength - along * along, 0.0F));
  return TwoBoneIkResult3D{
      .joint = add(root, add(scale(direction, along), scale(bend, height))),
      .end = end,
      .reached = reached,
  };
}

} // namespace demi::runtime
