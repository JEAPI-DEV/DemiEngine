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

void normalOffsetRespectsAngleAndFilterFootprint() {
  for (const float radius : {0.5F, 1.5F}) {
    assert(receiverNormalOffsetDistance(0, 0.8F, radius) == 0);
    assert(receiverNormalOffsetDistance(2, 1, radius) == 0);
    assert(receiverNormalOffsetDistance(2, 1.000001F, radius) == 0);
    assert(receiverNormalOffsetDistance(2, 0, radius) == 2 * radius);
    const float offset = receiverNormalOffsetDistance(2, 0.8F, radius);
    assert(std::abs(offset - 1.2F * radius) < 1e-6F);
  }
}

void adjacentFacetsAvoidFalseSelfShadowAndRetainADistinctOccluder() {
  constexpr float cosine = 0.8F;
  constexpr float normalHorizontal = 0.6F;
  constexpr float receiverSlope = normalHorizontal / cosine;
  constexpr float resolution = 512;
  for (const float scale : {0.01F, 1.0F, 1000.0F}) {
    const float shadowDiameter = 64 * scale;
    const float worldTexel = shadowDiameter / resolution;
    for (const float radius : {0.5F, 1.5F}) {
      const float normalOffset =
          receiverNormalOffsetDistance(worldTexel, cosine, radius);
      // A distinct parallel occluder is a quarter texel beyond the applied
      // normal offset. Contacts inside the offset are inherently unresolved.
      const float occluderNormalGap = normalOffset + 0.25F * worldTexel;
      for (const float side : {-1.0F, 1.0F}) {
        const auto surfaceDepth = [&](float x) {
          const float mirroredX = side * x;
          const float slope = mirroredX < 0 ? 1.5F : receiverSlope;
          return 0.5F + slope * mirroredX / shadowDiameter;
        };
        const float receiverX = side * 0.25F * worldTexel;
        const float receiverDepth = surfaceDepth(receiverX);
        const float crossingTap = -side * radius;
        const float unbiasedLookupX = receiverX + crossingTap * worldTexel;
        const float unbiasedDepth =
            receiverDepth +
            side * receiverSlope * crossingTap * worldTexel / shadowDiameter;
        // The receiver tangent overestimates the steeper neighboring facet:
        // this continuous surface really fails the unoffset shadow comparison.
        assert(unbiasedDepth > surfaceDepth(unbiasedLookupX) + 1e-6F);
        const float shiftedX =
            receiverX + side * normalHorizontal * normalOffset;
        const float shiftedDepth =
            receiverDepth - cosine * normalOffset / shadowDiameter;
        const auto gradient =
            receiverPlaneDepthGradient({1 / resolution, 0}, {0, 1 / resolution},
                                       {side * receiverSlope / resolution, 0});
        for (const float tap : {-radius, 0.0F, radius}) {
          const float lookupX = shiftedX + tap * worldTexel;
          const vec2 tapOffset{tap / resolution, 0};
          const float comparedDepth = shiftedDepth + dot(gradient, tapOffset);
          const float facetDepth = surfaceDepth(lookupX);
          assert(comparedDepth <= facetDepth + 1e-6F);
          const float currentFacetDepth =
              receiverDepth +
              side * receiverSlope * (lookupX - receiverX) / shadowDiameter;
          const float occluderDepth =
              std::min(facetDepth, currentFacetDepth) -
              occluderNormalGap / (cosine * shadowDiameter);
          assert(comparedDepth > occluderDepth);
        }
      }
    }
  }
}

} // namespace

int main() {
  normalOffsetRespectsAngleAndFilterFootprint();
  adjacentFacetsAvoidFalseSelfShadowAndRetainADistinctOccluder();
  tinyWellConditionedProjectionKeepsItsPlane();
  independentPixelScalesPreserveTheGradient();
  degenerateAndPoorlyConditionedProjectionUsesSafeFallback();
  planeCorrectionAvoidsSelfShadowAndRetainsRealOcclusion();
  std::cout << "Shadow receiver gradient checks passed\n";
}
