#include "demi/runtime/math/VectorMath.h"
#include "demi/runtime/render/bgfx3d/ShadowCascadeLayout3D.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {
std::vector<Vec3> receiverCorners(const BgfxCameraFrame3D &camera,
                                  const ShadowCascade3D &cascade) {
  using namespace demi::runtime::math;
  const auto forward = normalized(camera.forward);
  const auto right = normalized(cross(camera.up, forward));
  const auto up = normalized(cross(forward, right));
  const float aspect = float(camera.viewportWidth) / camera.viewportHeight;
  const float tangent = std::tan(camera.camera.fov * .008726646259971648F);
  std::vector<Vec3> corners;
  for (const auto depth : {cascade.receiverNear, cascade.receiverFar}) {
    const float halfHeight = camera.camera.perspective
                                 ? depth * tangent
                                 : camera.camera.orthographicSize * .5F;
    for (const auto x : {-halfHeight * aspect, halfHeight * aspect}) {
      for (const auto y : {-halfHeight, halfHeight}) {
        corners.push_back(
            add(camera.position, add(scale(forward, depth),
                                     add(scale(right, x), scale(up, y)))));
      }
    }
  }
  return corners;
}

void receiverCoverageIncludesFilteringExtrusionAndBlends() {
  using namespace demi::runtime::math;
  BgfxCameraFrame3D camera;
  camera.viewportWidth = 2560;
  camera.viewportHeight = 1440;
  camera.camera.fov = 58;
  camera.camera.orthographicSize = 70;
  camera.camera.farClip = 1800;
  camera.position = {-90, 43, -180};
  camera.forward = {30, -9, 17};
  camera.up = {.25F, 1.F, -.2F};
  SceneLighting3D light;
  light.direction = {-.45F, -.8F, .3F, 0};
  light.shadowDistance = 180;
  light.shadowResolution = 2048;
  std::string error;
  for (const bool perspective : {true, false}) {
    camera.camera.perspective = perspective;
    for (const float blend : {0.F, .1F, 1.F}) {
      light.shadowBlend = blend;
      for (const auto filter : {"hard", "pcf"}) {
        light.shadowFilter = filter;
        const auto layout = makeShadowCascadeLayout3D(light, camera, error);
        assert(layout && error.empty());
        for (int index = 0; index < layout->count; ++index) {
          const auto &cascade = layout->cascades[index];
          assert(cascade.casterHull.count >= 3);
          const auto right =
              normalized(Vec3{cascade.x[0], cascade.x[1], cascade.x[2]});
          const auto up =
              normalized(Vec3{cascade.y[0], cascade.y[1], cascade.y[2]});
          const auto center = add(
              cascade.eye, scale(cascade.direction, cascade.depthRange * .5F));
          bool rejectsUnusedProjectionCorner = false;
          for (const float x : {-.95F, .95F}) {
            for (const float y : {-.95F, .95F}) {
              const auto corner =
                  add(center, add(scale(right, x * cascade.radius),
                                  scale(up, y * cascade.radius)));
              rejectsUnusedProjectionCorner |=
                  !cascade.casterHull.intersectsSphere(corner, 0);
            }
          }
          assert(rejectsUnusedProjectionCorner);
          const float texel = 2 * cascade.radius / light.shadowResolution;
          const float tap = light.shadowFilter == "hard" ? .5F : 2.F;
          const float normal = light.shadowFilter == "hard" ? .5F : 1.5F;
          for (const auto corner : receiverCorners(camera, cascade)) {
            assert(cascade.casterHull.intersectsSphere(corner, 0));
            // Off-camera points aligned with a receiver along the light must
            // survive: camera-frustum culling alone would lose these casters.
            for (const float depth : {-.4F, .4F}) {
              const auto caster = add(
                  corner, scale(cascade.direction, depth * cascade.depthRange));
              assert(cascade.casterHull.intersectsSphere(caster, 0));
            }
            for (const float x : {-tap, tap}) {
              for (const float y : {-tap, tap}) {
                const auto sample = add(
                    corner, add(scale(right, x * texel), scale(up, y * texel)));
                for (const Vec3 direction :
                     {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 1, 0},
                      Vec3{0, -1, 0}, Vec3{0, 0, 1}, Vec3{0, 0, -1}}) {
                  assert(cascade.casterHull.intersectsSphere(
                      add(sample, scale(direction, normal * texel)), 0));
                }
              }
            }
          }
        }
      }
    }
  }
  // Translate the entire setup without projecting absolute world distances.
  camera.position = {1000000, -1000000, 1000000};
  const auto translated = makeShadowCascadeLayout3D(light, camera, error);
  assert(translated);
  for (int index = 0; index < translated->count; ++index) {
    const auto &cascade = translated->cascades[index];
    for (const auto corner : receiverCorners(camera, cascade)) {
      assert(cascade.casterHull.intersectsSphere(corner, 0));
      assert(cascade.casterHull.intersectsSphere(
          add(corner, scale(cascade.direction, .4F * cascade.depthRange)), 0));
    }
  }
}

void boundsStraddlesAndProjectionStability() {
  ShadowCasterHull3D hull;
  hull.count = 1;
  hull.planes[0] = {1, 0, 0, 0};
  assert(hull.intersectsSphere({1, 0, 0}, 1));
  assert(!hull.intersectsSphere({1.01F, 0, 0}, 1));
  assert(hull.intersectsAabb({0, -1, -1}, {2, 1, 1}, 0));
  assert(hull.intersectsAabb({1, -1, -1}, {2, 1, 1}, 1));
  assert(!hull.intersectsAabb({1.01F, -1, -1}, {2, 1, 1}, 1));
  hull.count = 0;
  assert(hull.intersectsSphere({10000, 0, 0}, 0));

  BgfxCameraFrame3D camera;
  camera.viewportWidth = 2560;
  camera.viewportHeight = 1440;
  camera.forward = {30, -9, 17};
  SceneLighting3D light;
  std::string error;
  light.shadowFilter = "hard";
  const auto hard = makeShadowCascadeLayout3D(light, camera, error);
  light.shadowFilter = "pcf";
  const auto pcf = makeShadowCascadeLayout3D(light, camera, error);
  assert(hard && pcf);
  for (int index = 0; index < hard->count; ++index) {
    const auto &a = hard->cascades[index];
    const auto &b = pcf->cascades[index];
    assert(a.x == b.x && a.y == b.y && a.z == b.z);
    assert(a.radius == b.radius && a.depthRange == b.depthRange);
    assert(a.casterHull.count == b.casterHull.count);
    for (std::size_t plane = 0; plane < a.casterHull.count; ++plane) {
      assert(a.casterHull.planes[plane][3] < b.casterHull.planes[plane][3]);
    }
  }
}
} // namespace

int main() {
  receiverCoverageIncludesFilteringExtrusionAndBlends();
  boundsStraddlesAndProjectionStability();
  std::cout << "Conservative receiver-hull caster culling checks passed\n";
}
