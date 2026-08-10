#include "demi/runtime/app/Bgfx2DAppHost.h"

#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

using namespace demi;
using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

void graphicsApiConfigurationIsSafe() {
  assert(setenv("DEMI_GRAPHICS_API", "noop", 1) == 0);
  assert(configuredGraphicsApi() == GraphicsApi::Noop);
  assert(setenv("DEMI_GRAPHICS_API", "not-a-renderer", 1) == 0);
  assert(configuredGraphicsApi() == GraphicsApi::Automatic);
}

void lifecycleAndRenderingAreOrdered() {
  assert(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
  Bgfx2DAppHost host;
  AssetRegistry assets;
  std::vector<std::string> diagnostics;
  std::string error;
  assert(!host.initialize(Bgfx2DAppHostConfig{.title = "invalid",
                                              .width = 0,
                                              .height = 180,
                                              .graphicsApi = GraphicsApi::Noop},
                          assets, diagnostics, error));
  assert(!error.empty());

  error.clear();
  assert(host.initialize(Bgfx2DAppHostConfig{.title = "bgfx 2D host test",
                                             .width = 320,
                                             .height = 180,
                                             .graphicsApi = GraphicsApi::Noop,
                                             .vsync = false},
                         assets, diagnostics, error));
  assert(host.rendererName() == "Noop");
  assert(!host.initialize(Bgfx2DAppHostConfig{}, assets, diagnostics, error));

  InputState input;
  host.poll(input);
  World world;
  world.hudCanvasSize = {320, 180};
  world.ui.canvasSize = {320, 180};
  world.ui.nodes.push_back(
      {.id = "panel",
       .type = "panel",
       .resolved = {.x = 10, .y = 10, .width = 40, .height = 30},
       .backgroundColor = {.r = 1.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F}});
  assert(
      host.renderFrame(world, Camera2DComponent{}, {}, 0.016F, nullptr, error));
  assert(!host.setWindowMode("invalid", error));
  assert(host.setWindowMode("windowed", error));

  host.shutdown();
  host.shutdown();
  assert(!host.renderFrame(world, Camera2DComponent{}, {}, 0.016F, nullptr,
                           error));
}

} // namespace

int main() {
  graphicsApiConfigurationIsSafe();
  lifecycleAndRenderingAreOrdered();
  return 0;
}
