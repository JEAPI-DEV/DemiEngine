#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/bgfx2d/TilemapCanvasRenderer.h"
#include "demi/runtime/scene/components/2dcomponents/Tilemap2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <cassert>
#include <string>
#include <unordered_map>

using namespace demi::runtime;
using namespace demi::runtime::render;

int main() {
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(GraphicsDeviceConfig{.api = GraphicsApi::Noop,
                                                  .width = 100,
                                                  .height = 100,
                                                  .vsync = false},
                             error));
  auto resources = createBgfxGpuResources();
  auto commands = createBgfxRenderCommands(*resources);
  Canvas2D canvas(*resources, *commands);
  TextureLibrary2D textures(*resources);
  assert(canvas.initialize(error));
  assert(textures.upload(
      "asset://tiles",
      ImageData2D{.width = 2,
                  .height = 1,
                  .rgba = std::vector<std::byte>(8, std::byte{0xff})},
      error));
  std::unordered_map<std::string, TilemapAsset2D> maps;
  maps.emplace("asset://map",
               TilemapAsset2D{
                   .tileWidth = 1,
                   .tileHeight = 1,
                   .columns = 2,
                   .rows = 1,
                   .layers = {{.name = "ground", .tiles = {1, 2}}},
                   .tilesets = {{.texture = "asset://tiles",
                                 .firstTile = 1,
                                 .tileWidth = 1,
                                 .tileHeight = 1}},
               });
  World world;
  Entity entity;
  entity.id = "map";
  entity.setComponent(Transform2DComponent{});
  entity.setComponent(
      Tilemap2DComponent{.asset = "asset://map", .pixelsPerUnit = 1});
  world.entities.push_back(std::move(entity));

  assert(canvas.begin(0, 100, 100, 0, error));
  TilemapCanvasRenderer renderer(canvas, textures, maps);
  assert(renderer.draw(world, Camera2DComponent{.orthographicSize = 2}, {},
                       100, 100, 0));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads == 2);
  static_cast<void>(graphics.endFrame());

  textures.clear();
  canvas.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
