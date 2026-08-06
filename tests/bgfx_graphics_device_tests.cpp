#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"

#include <iostream>
#include <limits>
#include <string>

namespace {

using demi::runtime::render::BgfxGraphicsDevice;
using demi::runtime::render::GraphicsApi;
using demi::runtime::render::GraphicsDeviceConfig;
using demi::runtime::render::graphicsApiName;
using demi::runtime::render::parseGraphicsApi;

bool apiNamesRoundTrip() {
  for (const GraphicsApi expected :
       {GraphicsApi::Automatic, GraphicsApi::Vulkan, GraphicsApi::OpenGL,
        GraphicsApi::OpenGLES, GraphicsApi::Noop}) {
    GraphicsApi parsed = GraphicsApi::Automatic;
    if (!parseGraphicsApi(graphicsApiName(expected), parsed) ||
        parsed != expected)
      return false;
  }

  GraphicsApi parsed = GraphicsApi::Automatic;
  return parseGraphicsApi("auto", parsed) &&
         parsed == GraphicsApi::Automatic && parseGraphicsApi("gles", parsed) &&
         parsed == GraphicsApi::OpenGLES &&
         !parseGraphicsApi("direct-rendering-manager", parsed);
}

bool invalidInitializationIsRejected() {
  BgfxGraphicsDevice device;
  std::string error;
  if (device.initialize(
          GraphicsDeviceConfig{.api = GraphicsApi::Noop, .width = 0},
          error) ||
      error.empty() || device.initialized())
    return false;

  error.clear();
  if (device.initialize(
          GraphicsDeviceConfig{.api = GraphicsApi::Noop,
                               .width = std::numeric_limits<std::uint16_t>::max() +
                                        1U},
          error) ||
      error.empty())
    return false;

  error.clear();
  return !device.initialize(
             GraphicsDeviceConfig{.api = GraphicsApi::Vulkan,
                                  .width = 64,
                                  .height = 64},
             error) &&
         !error.empty() && !device.initialized();
}

bool noopLifecycleIsSafe() {
  BgfxGraphicsDevice device;
  std::string error;
  if (!device.initialize(GraphicsDeviceConfig{.api = GraphicsApi::Noop,
                                               .width = 64,
                                               .height = 32,
                                               .vsync = false},
                         error) ||
      !device.initialized() || device.rendererName() != "Noop")
    return false;

  error.clear();
  if (device.initialize(GraphicsDeviceConfig{.api = GraphicsApi::Noop},
                        error) ||
      error.empty())
    return false;

  device.beginFrame(0x102030ffU);
  (void)device.endFrame();
  if (!device.resize(128, 72, error))
    return false;
  device.beginFrame(0x000000ffU);
  (void)device.endFrame();

  error.clear();
  if (device.resize(0, 72, error) || error.empty())
    return false;

  device.shutdown();
  device.shutdown();
  error.clear();
  return !device.initialized() && device.rendererName().empty() &&
         !device.resize(1, 1, error) && !error.empty() &&
         device.endFrame() == 0;
}

} // namespace

int main() {
  if (!apiNamesRoundTrip()) {
    std::cerr << "Graphics API name contract failed.\n";
    return 1;
  }
  if (!invalidInitializationIsRejected()) {
    std::cerr << "Invalid bgfx initialization contract failed.\n";
    return 1;
  }
  if (!noopLifecycleIsSafe()) {
    std::cerr << "bgfx lifecycle contract failed.\n";
    return 1;
  }
  std::cout << "bgfx graphics device tests passed.\n";
  return 0;
}
