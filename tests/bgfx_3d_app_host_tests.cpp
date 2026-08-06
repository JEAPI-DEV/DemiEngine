#include "demi/runtime/app/Bgfx3DAppHost.h"

#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace demi;
using namespace demi::runtime;
using namespace demi::runtime::render;

int main() {
  assert(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
  Bgfx3DAppHost host;
  AssetRegistry assets;
  std::vector<std::string> diagnostics;
  std::string error;
  assert(host.initialize({.title = "bgfx 3D host test",
                          .width = 320,
                          .height = 180,
                          .graphicsApi = GraphicsApi::Noop,
                          .vsync = false},
                         assets, diagnostics, error));
  assert(host.rendererName() == "Noop");
  assert(!host.initialize({}, assets, diagnostics, error));

  World world;
  world.hudCanvasSize = {320, 180};
  Entity cube;
  cube.id = "cube";
  cube.setComponent(Transform3DComponent{});
  cube.setComponent(MeshRendererComponent{.shape = "cube"});
  world.entities.push_back(std::move(cube));
  assert(host.renderFrame(world,
                          {.camera = Camera3DComponent{},
                           .position = {0.0F, 2.0F, -5.0F},
                           .forward = {0.0F, -0.2F, 1.0F}},
                          0.016F, error));

  host.shutdown();
  assets.assets.push_back(
      {.id = "asset://targets/test",
       .type = "RenderTarget",
       .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                     "examples/minimal_voxel/assets/targets/"
                     "voxel_minimap.target.json"});
  diagnostics.clear();
  error.clear();
  assert(host.initialize({.title = "bgfx scheduled target test",
                          .width = 320,
                          .height = 180,
                          .graphicsApi = GraphicsApi::Noop,
                          .vsync = false},
                         assets, diagnostics, error));
  BgfxCameraFrame3D targetFrame{
      .cameraId = "target-camera",
      .camera = {.renderTarget = "asset://targets/test",
                 .updateInterval = 1.0F},
      .position = {0.0F, 2.0F, -5.0F},
      .forward = {0.0F, -0.2F, 1.0F}};
  assert(host.renderFrame(world, targetFrame, 0.016F, error));
  assert(host.renderFrame(world, targetFrame, 0.016F, error));
  // Removing a scheduled camera prunes its timer; presenting another valid
  // camera between uses must not corrupt target ownership.
  assert(host.renderFrame(world,
                          {.cameraId = "main",
                           .camera = Camera3DComponent{},
                           .position = {0.0F, 2.0F, -5.0F},
                           .forward = {0.0F, -0.2F, 1.0F}},
                          0.016F, error));
  assert(host.renderFrame(world, targetFrame, 0.016F, error));

  host.shutdown();
  host.shutdown();
  assert(!host.renderFrame(world, {}, 0.016F, error));
  return 0;
}
