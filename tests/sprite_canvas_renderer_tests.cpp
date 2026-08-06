#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/bgfx2d/SpriteCanvasRenderer.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

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
      "asset://sprite",
      ImageData2D{.width = 2,
                  .height = 2,
                  .rgba = std::vector<std::byte>(16, std::byte{0xff})},
      error));
  assert(textures.upload(
      "asset://animated#2",
      ImageData2D{.width = 2,
                  .height = 2,
                  .rgba = std::vector<std::byte>(16, std::byte{0xff})},
      error));
  assert(textures.upload(
      "asset://gif#0",
      ImageData2D{.width = 1,
                  .height = 1,
                  .rgba = std::vector<std::byte>(4, std::byte{0xff})},
      error));

  World world;
  Entity textured;
  textured.id = "textured";
  textured.setComponent(Transform2DComponent{
      .position = {1, 2}, .rotation = 0.5F, .scale = {1, 1}});
  textured.setComponent(SpriteComponent{
      .texture = "asset://sprite",
      .size = {2, 1},
      .pivot = {0.25F, 0.75F},
      .flipX = true,
  });
  world.entities.push_back(std::move(textured));
  Entity circle;
  circle.id = "circle";
  circle.setComponent(Transform2DComponent{.position = {-1, 0}});
  circle.setComponent(
      SpriteComponent{.shape = "circle", .size = {1, 1}});
  world.entities.push_back(std::move(circle));
  Entity animated;
  animated.id = "animated";
  animated.setComponent(Transform2DComponent{.position = {0, -1}});
  animated.setComponent(
      SpriteComponent{.texture = "asset://animated", .size = {1, 1}});
  animated.setComponent(SpriteAnimator2DComponent{.currentFrame = 2});
  world.entities.push_back(std::move(animated));
  Entity gif;
  gif.id = "gif";
  gif.setComponent(Transform2DComponent{.position = {0, 1}});
  gif.setComponent(
      SpriteComponent{.texture = "asset://gif", .size = {1, 1}});
  world.entities.push_back(std::move(gif));

  assert(canvas.begin(0, 320, 180, 0, error));
  const std::unordered_map<std::string, TextureAnimation2D> animations{
      {"asset://gif", {.frameCount = 1, .frameDurations = {0.1F}}}};
  SpriteCanvasRenderer renderer(canvas, textures, &animations);
  assert(renderer.draw(world, Camera2DComponent{.orthographicSize = 5.0F},
                       {}, 320, 180));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads == 3);
  assert(canvas.statistics().triangles >= 38);
  static_cast<void>(graphics.endFrame());

  textures.clear();
  canvas.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
