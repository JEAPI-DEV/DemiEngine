#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/render/BgfxRenderer2D.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/SvgDecoder2D.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Tilemap2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace demi;
using namespace demi::runtime;
using namespace demi::runtime::render;

#if DEMI_HAS_RSVG
namespace {

void svgDimensionsAndIconAlpha() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("demi_svg_dimensions_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto writeSvg = [&](const char *name, const char *source) {
    const auto path = root / name;
    std::ofstream output(path);
    output << source;
    return path;
  };
  const auto background = writeSvg(
      "background.svg",
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="720" viewBox="0 0 1280 720"><rect width="1280" height="720" fill="red"/></svg>)");
  ImageData2D image;
  std::string error;
  assert(decodeSvg2D(background, false, image, error));
  assert(image.width == 1280 && image.height == 720);
  const auto channel = [](const ImageData2D &pixels, std::size_t x,
                          std::size_t y, std::size_t index) {
    return std::to_integer<unsigned char>(
        pixels.rgba[(y * pixels.width + x) * 4U + index]);
  };
  assert(channel(image, 0, 0, 0) == 255);
  assert(channel(image, 0, 0, 3) == 255);
  assert(channel(image, 1279, 719, 3) == 255);
  assert(decodeSvg2D(background, true, image, error));
  assert(image.width == 256 && image.height == 256);
  assert(channel(image, 128, 0, 3) == 0);
  assert(channel(image, 128, 128, 0) == 255);
  assert(channel(image, 128, 128, 1) == 255);
  assert(channel(image, 128, 128, 2) == 255);
  assert(channel(image, 128, 128, 3) == 255);

  const auto viewBox = writeSvg(
      "viewbox.svg",
      R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 320 180"><rect width="320" height="180" fill="blue"/></svg>)");
  assert(decodeSvg2D(viewBox, false, image, error));
  assert(image.width == 256 && image.height == 144);
  assert(channel(image, 0, 0, 2) == 255);
  assert(channel(image, 255, 143, 3) == 255);

  const auto icon = writeSvg(
      "icon.svg",
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32"><circle cx="16" cy="16" r="8" fill="red" opacity="0.5"/></svg>)");
  assert(decodeSvg2D(icon, true, image, error));
  assert(image.width == 256 && image.height == 256);
  assert(channel(image, 0, 0, 3) == 0);
  assert(channel(image, 128, 128, 0) == 255);
  assert(channel(image, 128, 128, 1) == 255);
  assert(channel(image, 128, 128, 2) == 255);
  const auto alpha = channel(image, 128, 128, 3);
  assert(alpha >= 127 && alpha <= 128);

  const auto oversized = writeSvg(
      "oversized.svg",
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="65536" height="1"/>)");
  assert(!decodeSvg2D(oversized, false, image, error));
  assert(!error.empty());
  assert(image.width == 0 && image.height == 0 && image.rgba.empty());
  std::filesystem::remove_all(root);
}

} // namespace
#endif

int main() {
#if DEMI_HAS_RSVG
  svgDimensionsAndIconAlpha();
#endif
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
  assert(renderer.residentTextureCount() == 0);
  assert(renderer.hasTexture("asset://sprites/player"));
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

  diagnostics.clear();
  const AssetRegistry isometricRegistry =
      loadAssetRegistry(std::filesystem::path(DEMI_SOURCE_DIR) /
                        "examples/isometric_base_builder");
  assert(renderer.loadAssets(isometricRegistry, diagnostics));
  assert(diagnostics.empty());
  assert(renderer.hasTexture("asset://ui/upgrade"));
  assert(renderer.hasTexture("asset://ui/destroy"));
  diagnostics.clear();
  assert(renderer.loadAssets(registry, diagnostics));
  assert(diagnostics.empty());

  World world;
  world.hudCanvasSize = {320, 180};
  world.ui.canvasSize = {320, 180};
  world.ui.nodes.push_back(
      {.id = "status",
       .type = "label",
       .text = "bgfx",
       .resolved = {.x = 4, .y = 4, .width = 100, .height = 24},
       .fontSize = 18});
  world.ui.nodes.push_back(
      {.id = "lazy_image",
       .type = "image",
       .texture = "asset://items/gold_coin",
       .resolved = {.x = 120, .y = 4, .width = 24, .height = 24}});
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
  assert(renderer.residentTextureCount() == 1);
  assert(renderer.drawHud(world));
  assert(renderer.residentTextureCount() == 2);
  assert(renderer.endFrame(error));
  assert(renderer.statistics().quads > 1);
  static_cast<void>(graphics.endFrame());
  assert(!renderer.endFrame(error));

  assert(renderer.beginFrameRegion(Camera2DComponent{.orthographicSize = 5.0F},
                                   {}, 7, 24, 32, 240, 120, 0.016F, error));
  assert(renderer.drawWorld(world, true));
  assert(renderer.endFrame(error));
  static_cast<void>(graphics.endFrame());

  const RenderTargetHandles embeddedTarget = resources->createRenderTarget(
      {.width = 240, .height = 120, .debugName = "embedded 2D test"}, error);
  assert(embeddedTarget.frameBuffer && embeddedTarget.color);
  assert(renderer.beginFrameRegion(Camera2DComponent{.orthographicSize = 5.0F},
                                   {}, 8, 24, 32, 240, 120, 0.016F, error, 1.0F,
                                   embeddedTarget.frameBuffer));
  assert(renderer.drawWorld(world));
  assert(renderer.endFrame(error));
  static_cast<void>(graphics.endFrame());
  assert(resources->destroy(embeddedTarget.frameBuffer));
  assert(resources->destroy(embeddedTarget.depth));
  assert(resources->destroy(embeddedTarget.color));

  AssetRegistry tilemapRegistry =
      loadAssetRegistry(std::filesystem::path(DEMI_SOURCE_DIR) /
                        "examples/production_2d_foundation");
  assert(tilemapRegistry.diagnostics.empty());
  std::erase_if(tilemapRegistry.assets, [](const AssetManifest &asset) {
    return asset.id != "asset://phase4/map" &&
           asset.id != "asset://phase4/terrain" &&
           asset.id != "asset://phase4/effects";
  });
  diagnostics.clear();
  assert(renderer.loadAssets(tilemapRegistry, diagnostics));
  assert(diagnostics.empty());
  World tilemapWorld;
  Entity tilemap;
  tilemap.id = "map";
  tilemap.setComponent(Transform2DComponent{.position = {-6.0F, -4.0F}});
  tilemap.setComponent(
      Tilemap2DComponent{.asset = "asset://phase4/map", .pixelsPerUnit = 1.0F});
  tilemapWorld.entities.push_back(std::move(tilemap));
  assert(renderer.beginFrame(Camera2DComponent{.orthographicSize = 9.0F}, {},
                             960, 540, 0.016F, error));
  assert(renderer.drawWorld(tilemapWorld));
  assert(renderer.endFrame(error));
  // 96 ground tiles, 42 wall tiles, and one animated effect tile.
  assert(renderer.statistics().quads == 139);
  static_cast<void>(graphics.endFrame());

  renderer.shutdown();
  renderer.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
