#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/render/BgfxRenderer2D.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/SvgDecoder2D.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

using namespace demi;
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
  BgfxRenderer2D renderer(*resources, *commands);

  assert(!renderer.beginFrame({}, {}, 320, 180, 0.016F, error));
  assert(renderer.initialize(error));
  assert(renderer.initialize(error));
  std::vector<std::string> diagnostics;
  const AssetRegistry registry = loadAssetRegistry(
      std::filesystem::path(DEMI_SOURCE_DIR) / "examples/minimal_2d_android");
  assert(registry.diagnostics.empty());
  assert(renderer.loadAssets(registry, diagnostics));
  assert(diagnostics.empty());
  assert(renderer.loadedTextureCount() >= 2);
  AssetRegistry fontRegistry;
  fontRegistry.assets.push_back(
      {.id = "asset://fonts/project-fallback",
       .type = "Font2D",
       .sourceHash = "test-font-revision",
       .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                     "fonts/Pixelify_Sans/static/PixelifySans-Bold.ttf"});
  diagnostics.clear();
  assert(renderer.loadAssets(fontRegistry, diagnostics));
  assert(diagnostics.empty());
  fontRegistry.assets.front().sourcePath = "missing-font.ttf";
  diagnostics.clear();
  assert(!renderer.loadAssets(fontRegistry, diagnostics));
  assert(diagnostics.size() == 1);
  diagnostics.clear();
  assert(renderer.loadAssets(registry, diagnostics));

  ImageData2D svgImage;
  error.clear();
  assert(!decodeSvg2D(std::filesystem::path(DEMI_SOURCE_DIR) /
                          "does-not-exist.svg",
                      false, svgImage, error));
  assert(!error.empty());

#if DEMI_HAS_RSVG
  diagnostics.clear();
  const AssetRegistry svgRegistry = loadAssetRegistry(
      std::filesystem::path(DEMI_SOURCE_DIR) / "examples/main_menu_gif");
  assert(svgRegistry.diagnostics.empty());
  assert(renderer.loadAssets(svgRegistry, diagnostics));
  assert(diagnostics.empty());
  assert(renderer.loadedTextureCount() >= 11);
  diagnostics.clear();
  assert(renderer.loadAssets(registry, diagnostics));
  assert(diagnostics.empty());
#endif

  World world;
  world.hudCanvasSize = {320, 180};
  world.ui.canvasSize = {320, 180};
  world.ui.nodes.push_back(
      {.id = "status",
       .type = "label",
       .text = "bgfx",
       .resolved = {.x = 4, .y = 4, .width = 100, .height = 24},
       .fontSize = 18});
  Entity sprite;
  sprite.id = "sprite";
  sprite.setComponent(Transform2DComponent{});
  sprite.setComponent(
      SpriteComponent{.texture = "asset://sprites/player", .size = {1, 1}});
  world.entities.push_back(std::move(sprite));

  assert(renderer.beginFrame(Camera2DComponent{.orthographicSize = 5.0F}, {},
                             320, 180, 0.016F, error));
  assert(!renderer.beginFrame({}, {}, 320, 180, 0.016F, error));
  assert(renderer.drawWorld(world));
  assert(renderer.drawHud(world));
  assert(renderer.endFrame(error));
  assert(renderer.statistics().quads > 1);
  static_cast<void>(graphics.endFrame());
  assert(!renderer.endFrame(error));

  renderer.shutdown();
  renderer.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
