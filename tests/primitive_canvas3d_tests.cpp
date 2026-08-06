#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/render/bgfx3d/PrimitiveCanvas3D.h"

#include <array>
#include <cassert>
#include <string>

using namespace demi::runtime;
using namespace demi::runtime::render;

int main() {
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
                          .target = {0.0F, 0.0F, 0.0F}};
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
