#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>

namespace {

// Minimal GLSL arithmetic adapter: the solver below is the production shader
// helper itself, not a separately maintained C++ implementation.
struct vec2 {
  float x;
  float y;
  explicit vec2(float value) : x(value), y(value) {}
  vec2(float first, float second) : x(first), y(second) {}
};
vec2 operator/(vec2 value, float scale) {
  return {value.x / scale, value.y / scale};
}
vec2 operator/(vec2 value, vec2 scale) {
  return {value.x / scale.x, value.y / scale.y};
}
vec2 operator*(vec2 value, float scale) {
  return {value.x * scale, value.y * scale};
}
float dot(vec2 first, vec2 second) {
  return first.x * second.x + first.y * second.y;
}
using std::abs;
using std::max;
using std::sqrt;

#include "demi/runtime/render/bgfx3d/shaders/demi_shadow_receiver.sh"

void requireGradient(vec2 rowX, vec2 rowY, vec2 expected) {
  const vec2 depthDerivative{dot(rowX, expected), dot(rowY, expected)};
  const auto gradient = receiverPlaneDepthGradient(rowX, rowY, depthDerivative);
  assert(std::abs(gradient.x - expected.x) < 1e-5F);
  assert(std::abs(gradient.y - expected.y) < 1e-5F);
}

void tinyWellConditionedProjectionKeepsItsPlane() {
  const auto gradient =
      receiverPlaneDepthGradient({1e-7F, 0}, {0, 1e-7F}, {2.5e-8F, 5e-8F});
  assert(gradient.x == 0.25F && gradient.y == 0.5F);
}

void independentPixelScalesPreserveTheGradient() {
  constexpr std::array scales{1e-9F, 1e-7F, 1e-3F, 1.0F, 1e4F};
  for (const float x : scales) {
    for (const float y : scales) {
      requireGradient(vec2{0.4F, 0.15F} * x, vec2{-0.2F, 0.7F} * y,
                      {0.25F, 0.5F});
    }
  }
  // Inverting the atlas V axis must invert only the V depth gradient.
  requireGradient({0.4F, -0.15F}, {-0.2F, -0.7F}, {0.25F, -0.5F});
}

void degenerateAndPoorlyConditionedProjectionUsesSafeFallback() {
  for (const auto rows :
       {std::array{vec2{0, 0}, vec2{0, 1}}, std::array{vec2{1, 2}, vec2{2, 4}},
        std::array{vec2{1, 1}, vec2{1, 1.000001F}}}) {
    const auto gradient =
        receiverPlaneDepthGradient(rows[0], rows[1], {0.5F, 0.8F});
    assert(gradient.x == 0 && gradient.y == 0);
  }
}

void planeCorrectionAvoidsSelfShadowAndRetainsRealOcclusion() {
  constexpr float receiverDepth = 0.5F;
  constexpr float bias = 0.00001F;
  const vec2 expected{0.25F, 0.5F};
  const auto gradient =
      receiverPlaneDepthGradient({1e-7F, 0}, {0, 1e-7F}, {2.5e-8F, 5e-8F});
  for (const float x : {-1.5F, -0.5F, 0.5F, 1.5F}) {
    for (const float y : {-1.5F, -0.5F, 0.5F, 1.5F}) {
      const vec2 tapOffset{x / 2048.0F, y / 2048.0F};
      const float planeDepth = receiverDepth + dot(expected, tapOffset);
      const float comparedDepth =
          receiverDepth - bias + dot(gradient, tapOffset);
      assert(comparedDepth <= planeDepth);
      const float occluderDepth = planeDepth - 0.01F;
      assert(comparedDepth > occluderDepth);
    }
  }
}

} // namespace

int main() {
  tinyWellConditionedProjectionKeepsItsPlane();
  independentPixelScalesPreserveTheGradient();
  degenerateAndPoorlyConditionedProjectionUsesSafeFallback();
  planeCorrectionAvoidsSelfShadowAndRetainsRealOcclusion();
  std::cout << "Shadow receiver gradient checks passed\n";
}
