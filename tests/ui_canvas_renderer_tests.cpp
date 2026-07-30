#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/bgfx2d/UiCanvasRenderer.h"

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
  FontAtlas2D font(*resources);
  TextureLibrary2D textures(*resources);
  assert(canvas.initialize(error));
  assert(font.initializeDefault(48.0F, error));

  ui::UiDocument document;
  document.canvasSize = {320, 180};
  document.nodes = {
      {.id = "scroll",
       .type = "scroll",
       .resolved = {.x = 5, .y = 5, .width = 100, .height = 60},
       .backgroundColor = {.r = 0.1F, .g = 0.2F, .b = 0.3F, .a = 1.0F}},
      {.id = "label",
       .parent = "scroll",
       .type = "label",
       .text = "Clipped",
       .resolved = {.x = 80, .y = 10, .width = 100, .height = 20},
       .fontSize = 16},
      {.id = "button",
       .type = "button",
       .text = "Play",
       .resolved = {.x = 120, .y = 80, .width = 80, .height = 30},
       .backgroundColor = {.r = 0.3F, .g = 0.4F, .b = 0.5F, .a = 1.0F},
       .borderColor = {.r = 1, .g = 1, .b = 1, .a = 1},
       .fontSize = 14,
       .borderWidth = 1},
      {.id = "progress",
       .type = "progress",
       .resolved = {.x = 20, .y = 130, .width = 200, .height = 10},
       .color = {.r = 0, .g = 1, .b = 0, .a = 1},
       .backgroundColor = {.r = 0, .g = 0, .b = 0, .a = 1},
       .value = 0.5F},
      {.id = "circle",
       .type = "circle",
       .resolved = {.x = 250, .y = 120, .width = 30, .height = 30},
       .color = {.r = 1, .g = 0, .b = 0, .a = 1},
       .radius = 15},
  };

  assert(canvas.begin(0, 320, 180, 0, error));
  UiCanvasRenderer renderer(canvas, font);
  assert(renderer.draw(document, 320, 180));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads > 5);
  assert(canvas.statistics().triangles >= 32);
  static_cast<void>(graphics.endFrame());

  const ImageData2D pixel{.width = 1,
                          .height = 1,
                          .rgba = {std::byte{0xff}, std::byte{0xff},
                                   std::byte{0xff}, std::byte{0xff}}};
  assert(textures.upload("asset://walk#2", pixel, error));
  assert(textures.upload("asset://gif#1", pixel, error));
  ui::UiDocument animatedDocument;
  animatedDocument.canvasSize = {320, 180};
  animatedDocument.nodes = {
      {.id = "walker",
       .type = "image",
       .texture = "asset://walk",
       .animation = "asset://walk",
       .resolved = {.x = 10, .y = 10, .width = 32, .height = 32},
       .animationFrame = 10},
      {.id = "gif",
       .type = "image",
       .texture = "asset://gif",
       .resolved = {.x = 50, .y = 10, .width = 32, .height = 32}},
  };
  const std::unordered_map<std::string, TextureAnimation2D> animations{
      {"asset://walk", {.frameCount = 4}},
      {"asset://gif", {.frameCount = 2, .frameDurations = {0.1F, 0.1F}}},
  };
  assert(canvas.begin(0, 320, 180, 0, error));
  UiCanvasRenderer animatedRenderer(
      canvas, font,
      [&textures](const std::string_view id) { return textures.find(id); },
      &animations, 0.15F);
  assert(animatedRenderer.draw(animatedDocument, 320, 180));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads == 2);
  static_cast<void>(graphics.endFrame());

  textures.clear();
  font.shutdown();
  canvas.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
