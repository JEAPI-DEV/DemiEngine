#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>

namespace {

struct vec3 {
  float x;
  float y;
  float z;
  vec3() = default;
  explicit vec3(float value) : x(value), y(value), z(value) {}
  vec3(float first, float second, float third)
      : x(first), y(second), z(third) {}
  vec3 operator+(vec3 other) const {
    return {x + other.x, y + other.y, z + other.z};
  }
  vec3 operator*(float scale) const {
    return {x * scale, y * scale, z * scale};
  }
  vec3 operator/(float scale) const {
    return {x / scale, y / scale, z / scale};
  }
  void operator*=(float scale) { *this = *this * scale; }
  void operator/=(float scale) { *this = *this / scale; }
};

float dot(vec3 first, vec3 second) {
  return first.x * second.x + first.y * second.y + first.z * second.z;
}
vec3 cross(vec3 first, vec3 second) {
  return {first.y * second.z - first.z * second.y,
          first.z * second.x - first.x * second.z,
          first.x * second.y - first.y * second.x};
}
float inversesqrt(float value) { return 1.0F / std::sqrt(value); }
using std::abs;
using std::max;

#include "demi/runtime/render/bgfx3d/shaders/demi_terrain_normal_frame.sh"

struct DoubleVector {
  double x;
  double y;
  double z;
  DoubleVector(vec3 value) : x(value.x), y(value.y), z(value.z) {}
  DoubleVector(double first, double second, double third)
      : x(first), y(second), z(third) {}
  DoubleVector operator+(DoubleVector other) const {
    return {x + other.x, y + other.y, z + other.z};
  }
  DoubleVector operator*(double scale) const {
    return {x * scale, y * scale, z * scale};
  }
};

DoubleVector doubleCross(DoubleVector first, DoubleVector second) {
  return {first.y * second.z - first.z * second.y,
          first.z * second.x - first.x * second.z,
          first.x * second.y - first.y * second.x};
}

std::array<double, 2> project(vec3 value, int axis, vec3 localNormal) {
  if (axis == 0) {
    return {localNormal.x < 0 ? value.z : -value.z, value.y};
  }
  if (axis == 1) {
    return {value.x, localNormal.y < 0 ? -value.z : value.z};
  }
  return {localNormal.z < 0 ? -value.x : value.x, value.y};
}

// Independent double-precision cotangent construction. World derivatives are
// used directly; the shader's stabilizing common scales cancel analytically.
vec3 referenceFrame(vec3 normal, vec3 worldDx, vec3 worldDy, vec3 localDx,
                    vec3 localDy, vec3 localNormal, vec3 mapped, int axis,
                    float tiling) {
  if (terrainFrameExtent(worldDx) == 0 || terrainFrameExtent(worldDy) == 0) {
    return normal;
  }
  const auto dx = project(localDx, axis, localNormal);
  const auto dy = project(localDy, axis, localNormal);
  const auto perpendicularY = doubleCross(worldDy, normal);
  const auto perpendicularX = doubleCross(normal, worldDx);
  const auto tangent =
      perpendicularY * (dx[0] * tiling) + perpendicularX * (dy[0] * tiling);
  const auto bitangent =
      perpendicularY * (dx[1] * tiling) + perpendicularX * (dy[1] * tiling);
  const double length =
      std::max(std::hypot(tangent.x, tangent.y, tangent.z),
               std::hypot(bitangent.x, bitangent.y, bitangent.z));
  if (length == 0) {
    return normal;
  }
  const auto result = tangent * (mapped.x / length) +
                      bitangent * (mapped.y / length) +
                      DoubleVector(normal) * mapped.z;
  const double resultLength = std::hypot(result.x, result.y, result.z);
  if (resultLength == 0) {
    return normal;
  }
  return {float(result.x / resultLength), float(result.y / resultLength),
          float(result.z / resultLength)};
}

void expectReference(vec3 normal, vec3 worldDx, vec3 worldDy, vec3 localDx,
                     vec3 localDy, vec3 localNormal, vec3 mapped,
                     float tiling) {
  const auto basis =
      terrainNormalBasis(normal, worldDx, worldDy, localDx, localDy);
  for (int axis = 0; axis < 3; ++axis) {
    const auto actual =
        terrainNormalFrame(normal, basis, axis, localNormal, mapped);
    const auto expected =
        referenceFrame(normal, worldDx, worldDy, localDx, localDy, localNormal,
                       mapped, axis, tiling);
    const float error = std::hypot(actual.x - expected.x, actual.y - expected.y,
                                   actual.z - expected.z);
    assert(std::isfinite(error) && error < 0.0001F);
  }
}

void transformedFramesMatchCotangentReference() {
  std::mt19937 random(47021);
  std::uniform_real_distribution<float> component(-1, 1);
  std::uniform_real_distribution<float> exponent(-4, 4);
  const vec3 up{0, 1, 0};
  for (int sample = 0; sample < 1000; ++sample) {
    const vec3 localDx{component(random), component(random), component(random)};
    const vec3 localDy{component(random), component(random), component(random)};
    const auto localNormal = terrainNormalize(cross(localDx, localDy), up);
    const vec3 scale{std::pow(10.0F, exponent(random)),
                     std::pow(10.0F, exponent(random)),
                     std::pow(10.0F, exponent(random))};
    const auto transform = [&](vec3 value) {
      // Nonuniform scale, shear, then a fixed rotation; not an axis-aligned
      // world mapping that could accidentally hide local/world confusion.
      const float x = value.x * scale.x + value.y * scale.y * 0.2F;
      const float y = value.y * scale.y + value.z * scale.z * 0.3F;
      const float z = value.z * scale.z;
      return vec3{0.8F * x + 0.6F * z, y, -0.6F * x + 0.8F * z};
    };
    const auto worldDx = transform(localDx);
    const auto worldDy = transform(localDy);
    const auto normal = terrainNormalize(cross(worldDx, worldDy), up);
    const auto mapped =
        terrainNormalize(vec3{component(random), component(random),
                              0.5F + std::abs(component(random))},
                         up);
    expectReference(normal, worldDx, worldDy, localDx, localDy, localNormal,
                    mapped, std::pow(10.0F, exponent(random)));
  }
}

void degenerateAndExtremeFramesKeepStablePairScaling() {
  const vec3 normal{0, 0, 1};
  const vec3 localNormal{0.2F, -0.3F, 0.9F};
  const auto mapped = terrainNormalize(vec3{0.3F, -0.4F, 1}, normal);
  const vec3 dx{1, 0, 0};
  const vec3 dy{0, 1, 0};
  expectReference(normal, dx, dy, vec3{1.0e-30F, 0, 1.0e30F},
                  vec3{0, 1.0e-30F, 1.0e30F}, localNormal, mapped, 1);
  for (const float scale : {1.0e-30F, 1.0e30F}) {
    expectReference(normal, dx, dy, vec3{scale, 0, scale},
                    vec3{0, scale, scale}, localNormal, mapped, 1);
  }
  expectReference(normal, vec3{0}, dy, dx, dy, localNormal, mapped, 1);
  expectReference(normal, dx, vec3{0}, dx, dy, localNormal, mapped, 1);
  expectReference(normal, dx, dy, vec3{0}, vec3{0}, localNormal, mapped, 1);
}

} // namespace

int main() {
  transformedFramesMatchCotangentReference();
  degenerateAndExtremeFramesKeepStablePairScaling();
  std::cout << "Terrain normal gradients match cotangent reference\n";
}
