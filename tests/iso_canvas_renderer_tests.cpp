#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/bgfx2d/IsoCanvasRenderer.h"
#include "demi/runtime/scene/components/2dcomponents/IsoGridComponent.h"
#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/gameplay/BuildableComponent.h"

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
  Canvas2D canvas(*resources, *commands);
  TextureLibrary2D textures(*resources);
  assert(canvas.initialize(error));
  assert(textures.upload(
      "asset://tile",
      ImageData2D{.width = 4,
                  .height = 4,
                  .rgba = std::vector<std::byte>(64, std::byte{0xff})},
      error));
  assert(textures.upload(
      "asset://tower",
      ImageData2D{.width = 4,
                  .height = 8,
                  .rgba = std::vector<std::byte>(128, std::byte{0xff})},
      error));

  World world;
  Entity grid;
  grid.id = "grid";
  grid.setComponent(IsoGridComponent{.cellSize = {1, 0.5F},
                                     .width = 3,
                                     .height = 2,
                                     .defaultTexture = "asset://tile",
                                     .cellTextures = {{"1,1", "missing"}}});
  world.entities.push_back(std::move(grid));
  Entity tower;
  tower.id = "tower";
  tower.setComponent(
      IsoTransformComponent{.tile = {1, 1}, .footprint = {2, 1}});
  tower.setComponent(SpriteComponent{.texture = "asset://tower",
                                     .size = {1.5F, 2.0F},
                                     .pivot = {0.5F, 1.0F}});
  tower.setComponent(
      BuildableComponent{.asset = "asset://tower", .blocksMovement = true});
  world.entities.push_back(std::move(tower));
  Entity fallback;
  fallback.id = "fallback";
  fallback.setComponent(IsoTransformComponent{.tile = {0, 0}});
  fallback.setComponent(BuildableComponent{.asset = "missing"});
  world.entities.push_back(std::move(fallback));

  assert(canvas.begin(0, 320, 180, 0, error));
  IsoCanvasRenderer renderer(canvas, textures);
  assert(renderer.draw(world, Camera2DComponent{.orthographicSize = 5}, {},
                       320, 180));
  assert(canvas.flush(error));
  // Five textured tiles, one textured entity, one fallback entity, and the
  // grid outlines should all reach the shared batch.
  assert(canvas.statistics().quads >= 7);
  static_cast<void>(graphics.endFrame());

  // No grid is a valid non-isometric scene, not an error.
  world.entities.erase(world.entities.begin());
  assert(canvas.begin(0, 320, 180, 0, error));
  assert(renderer.draw(world, Camera2DComponent{}, {}, 320, 180));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads == 0);
  static_cast<void>(graphics.endFrame());

  textures.clear();
  canvas.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
