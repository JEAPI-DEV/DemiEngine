#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/BgfxView3D.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/render/bgfx3d/PrimitiveCanvas3D.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <string>

using namespace demi::runtime;
using namespace demi::runtime::render;

void distantViewKeepsItsOrientationAndShadowProjection() {
  View3DConfig local;
  local.forward = {-0.6F, -0.75F, -0.3F};
  local.up = {-2.0F / 3.0F, 2.0F / 3.0F, -1.0F / 3.0F};
  const auto originMatrix = bgfxViewMatrix3D(local);
  const std::array<double, 3> direction{local.forward.x, local.forward.y,
                                        local.forward.z};
  const double directionLength =
      std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] +
                direction[2] * direction[2]);
  for (std::size_t row = 0; row < 3; ++row) {
    double projectedDirection = 0;
    double axisLengthSquared = 0;
    for (std::size_t column = 0; column < 3; ++column) {
      const double axis = originMatrix[column * 4 + row];
      projectedDirection += axis * direction[column];
      axisLengthSquared += axis * axis;
    }
    assert(std::abs(axisLengthSquared - 1) < 1e-6);
    const double expected = row == 2 ? -directionLength : 0;
    assert(std::abs(projectedDirection - expected) < 1e-6);
  }
  const std::array<Point3D, 4> eyes{{
      {1000, 350, 1850},
      {1050, 600, 2250},
      {1000000, -1000000, 1000000},
      {100000000, -100000000, 100000000},
  }};
  for (const auto eye : eyes) {
    auto distant = local;
    distant.eye = eye;
    const auto distantMatrix = bgfxViewMatrix3D(distant);
    for (std::size_t column = 0; column < 3; ++column) {
      for (std::size_t row = 0; row < 3; ++row) {
        const std::size_t index = column * 4 + row;
        assert(std::isfinite(distantMatrix[index]));
        assert(distantMatrix[index] == originMatrix[index]);
      }
    }
    const std::array<double, 3> relativePoint{256, 384, 512};
    const std::array<double, 3> eyeCoordinates{eye.x, eye.y, eye.z};
    for (std::size_t row = 0; row < 3; ++row) {
      double receiverCoordinate = 0;
      double casterCoordinate = distantMatrix[12 + row];
      for (std::size_t column = 0; column < 3; ++column) {
        const double axis = originMatrix[column * 4 + row];
        receiverCoordinate += axis * relativePoint[column];
        casterCoordinate +=
            axis * (eyeCoordinates[column] + relativePoint[column]);
      }
      // Translation remains float; orientation and shadow coordinates must
      // agree within its final rounding, without an eye-dependent basis.
      const double translationTolerance =
          std::max(0.001, std::abs(double(distantMatrix[12 + row])) * 1e-7);
      assert(std::abs(casterCoordinate - receiverCoordinate) <=
             translationTolerance);
    }
  }
}

int main() {
  distantViewKeepsItsOrientationAndShadowProjection();
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(GraphicsDeviceConfig{.api = GraphicsApi::Noop,
                                                  .width = 320,
                                                  .height = 180,
                                                  .vsync = false},
                             error));
  auto resources = createBgfxGpuResources();
  auto commands = createBgfxRenderCommands(*resources);
  PrimitiveCanvas3D canvas(*resources, *commands);

  const View3DConfig view{.width = 320,
                          .height = 180,
                          .eye = {0.0F, 2.0F, 5.0F},
                          .forward = {0.0F, -2.0F, -5.0F}};
  assert(!canvas.begin(view, error));
  assert(canvas.initialize(error));
  assert(canvas.initialize(error));
  assert(!canvas.begin(View3DConfig{.width = 0}, error));
  assert(error.find("positive") != std::string::npos);
  assert(!canvas.begin(
      View3DConfig{.width = 1, .height = 1, .nearClip = 2.0F, .farClip = 1.0F},
      error));
  assert(error.find("near < far") != std::string::npos);
  assert(!canvas.begin(
      View3DConfig{.width = 1, .height = 1, .verticalFovDegrees = 180.0F},
      error));
  assert(error.find("field of view") != std::string::npos);

  assert(canvas.begin(view, error));
  assert(!canvas.begin(view, error));
  const WorldTransform3D transform{.position = {1.0F, 2.0F, 3.0F},
                                   .rotation = {0.1F, 0.2F, 0.3F},
                                   .scale = {2.0F, 1.0F, 0.5F}};
  assert(!canvas.cube(transform, {-1.0F, 1.0F, 1.0F}, 0xffffffffU));
  assert(canvas.cube(transform, {1.0F, 2.0F, 3.0F}, 0xffffffffU));
  assert(canvas.plane(transform, {2.0F, 1.0F, 4.0F}, 0xff00ffffU));
  assert(!canvas.sphere(transform, 1.0F, 0xffffffffU, 2, 8));
  assert(canvas.sphere(transform, 1.0F, 0xffffffffU, 4, 2));
  assert(!canvas.cylinder(transform, {1.0F, 2.0F, 1.0F}, 0xffffffffU, 2));
  assert(canvas.cylinder(transform, {1.0F, 2.0F, 1.0F}, 0xffffffffU, 4));
  constexpr std::array<Vec3, 4> InvalidTriangles{};
  assert(!canvas.triangles(transform, InvalidTriangles, 0xffffffffU));
  constexpr std::array<Vec3, 3> Triangle{
      {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}}};
  assert(canvas.triangles(transform, Triangle, 0xffffffffU));
  assert(canvas.line({-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 0xff4040ffU));
  assert(canvas.flush(error));
  assert(canvas.statistics().drawCalls == 2);
  assert(canvas.statistics().vertices == 42);
  assert(canvas.statistics().indices == 143);
  assert(canvas.statistics().triangles == 47);
  assert(!canvas.flush(error));
  static_cast<void>(graphics.endFrame());

  canvas.shutdown();
  canvas.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
