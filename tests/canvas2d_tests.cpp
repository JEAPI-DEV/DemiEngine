#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/Canvas2D.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <string>

using namespace demi::runtime::render;

int main() {
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(
      GraphicsDeviceConfig{
          .api = GraphicsApi::Noop, .width = 100, .height = 80, .vsync = false},
      error));
  auto resources = createBgfxGpuResources();
  auto commands = createBgfxRenderCommands(*resources);
  Canvas2D canvas(*resources, *commands, 4);

  assert(!canvas.begin(0, 100, 80, 0, error));
  assert(canvas.initialize(error));
  assert(canvas.initialize(error));
  assert(canvas.begin(0, 100, 80, 0x102030ffU, error));

  assert(!canvas.solid(Rect2D{}, 0xffffffffU));
  assert(canvas.solid(Rect2D{.x = 2, .y = 3, .width = 10, .height = 11},
                      0xffffffffU));
  assert(canvas.image(canvas.whiteTexture(),
                      Rect2D{.x = 20, .y = 3, .width = 10, .height = 11}, {},
                      0xff00ffffU, BlendMode::Alpha,
                      ScissorRect{.x = 0, .y = 0, .width = 40, .height = 40}));
  assert(canvas.imageTransformed(canvas.whiteTexture(), 70, 20, 10, 12, 0.5F,
                                 0.5F, 0.25F));
  assert(canvas.ninePatch(
      canvas.whiteTexture(), Rect2D{.x = 5, .y = 20, .width = 30, .height = 20},
      {},
      NinePatch2D{
          .left = 5.0F,
          .top = 5.0F,
          .right = 5.0F,
          .bottom = 5.0F,
          .center = {.u0 = 0.25F, .v0 = 0.25F, .u1 = 0.75F, .v1 = 0.75F}}));
  assert(!canvas.ninePatch(canvas.whiteTexture(),
                           Rect2D{.width = 30, .height = 20}, {},
                           NinePatch2D{.center = {.u0 = 0.8F, .u1 = 0.2F}}));
  assert(!canvas.circle(50.0F, 40.0F, 4.0F, 0xffffffffU, 2));
  assert(canvas.circle(50.0F, 40.0F, 4.0F, 0xffffffffU, 8));

  assert(canvas.flush(error));
  const Canvas2DStatistics stats = canvas.statistics();
  assert(stats.quads == 12);
  // Solid, clipped image, and nine-patch are separate compatibility groups;
  // maxQuadsPerDraw also splits the nine-patch into 4 + 4 + 1.
  assert(stats.drawCalls == 7);
  assert(stats.vertices == 72);
  assert(stats.indices == 96);
  assert(stats.triangles == 32);
  const ScissorRect embedded = canvasScissorForView(
      {.x = 3, .y = 5, .width = 40, .height = 30}, 240, 120);
  assert(embedded.x == 243 && embedded.y == 125);
  assert(embedded.width == 40 && embedded.height == 30);
  assert(canvasScissorForView({}, 240, 120) == ScissorRect{});
  static_cast<void>(graphics.endFrame());

  assert(canvas.begin(0, 100, 80, 0, error));
  assert(!canvas.line(1, 1, 1, 1, 2, 0xffffffffU));
  assert(canvas.line(1, 1, 20, 15, 2, 0xffffffffU));
  assert(!canvas.circleOutline(50, 40, 10, 2, 0xffffffffU, 2));
  assert(canvas.circleOutline(50, 40, 10, 2, 0xffffffffU, 12));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads == 13);
  assert(canvas.statistics().triangles == 26);
  static_cast<void>(graphics.endFrame());

  // Material uniform sets participate in the batch key so two sprites using
  // the same texture/program cannot accidentally share parameter values.
  const UniformHandle tint =
      resources->createUniform("u_canvas_test", UniformType::Vec4, 1, error);
  assert(tint);
  const std::array<float, 4> red{1.0F, 0.0F, 0.0F, 1.0F};
  const std::array<float, 4> blue{0.0F, 0.0F, 1.0F, 1.0F};
  const std::array<DrawUniformValue, 1> redUniform{{
      {.handle = tint, .values = red},
  }};
  const std::array<DrawUniformValue, 1> blueUniform{{
      {.handle = tint, .values = blue},
  }};
  assert(canvas.begin(0, 100, 80, 0, error));
  canvas.setUniformSet(1, redUniform);
  canvas.setUniformSet(2, blueUniform);
  assert(canvas.solid({.width = 10, .height = 10}, 0xffffffffU,
                      BlendMode::Alpha, {}, canvas.program(), 1));
  assert(canvas.solid({.x = 12, .width = 10, .height = 10}, 0xffffffffU,
                      BlendMode::Alpha, {}, canvas.program(), 2));
  assert(canvas.flush(error));
  assert(canvas.statistics().drawCalls == 2);
  static_cast<void>(graphics.endFrame());
  assert(resources->destroy(tint));

  canvas.shutdown();
  canvas.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
