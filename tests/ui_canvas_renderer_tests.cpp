#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/bgfx2d/UiCanvasRenderer.h"

#include <cassert>
#include <limits>
#include <string>
#include <vector>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

class CapturingRenderCommands final : public RenderCommands {
public:
  bool configureView2D(const View2DConfig &, std::string &) override {
    return true;
  }
  bool configureView3D(const View3DConfig &, std::string &) override {
    return true;
  }
  bool submit(const TransientDraw &draw, std::string &) override {
    scissors.push_back(draw.scissor);
    return true;
  }
  bool submit(const BufferedDraw &, std::string &) override { return true; }
  bool submit(const InstancedBufferedDraw &, std::string &) override {
    return true;
  }

  std::vector<ScissorRect> scissors;
};

} // namespace

int main() {
  const Rect2D intrinsic =
      uiTextBounds({.x = 28, .y = 26}, 214.0F, 18.0F);
  const Rect2D widthOnly = uiTextBounds(
      {.x = 10, .y = 12, .width = 160, .height = 0}, 90.0F, 22.0F);
  const Rect2D explicitBounds = uiTextBounds(
      {.x = 1, .y = 2, .width = 80, .height = 30}, 200.0F, 50.0F);
  assert(intrinsic.x == 28.0F && intrinsic.y == 26.0F &&
         intrinsic.width == 214.0F && intrinsic.height == 18.0F);
  assert(widthOnly.width == 160.0F && widthOnly.height == 22.0F);
  assert(explicitBounds.width == 80.0F && explicitBounds.height == 30.0F);
  assert(uiCaretVisible(0.0F));
  assert(uiCaretVisible(0.54F));
  assert(!uiCaretVisible(0.55F));
  assert(!uiCaretVisible(0.99F));
  assert(uiCaretVisible(1.0F));
  assert(uiCaretVisible(-1.0F));
  assert(uiCaretVisible(std::numeric_limits<float>::infinity()));
  assert(uiCaretVisible(std::numeric_limits<float>::quiet_NaN()));

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
      {.id = "intrinsic_label",
       .type = "label",
       .text = "Position only",
       .resolved = {.x = 12, .y = 155},
       .fontSize = 12},
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

  ui::UiDocument editingDocument;
  editingDocument.canvasSize = {320, 180};
  editingDocument.focusedId = "name";
  editingDocument.nodes = {{.id = "name",
                            .type = "text_input",
                            .text = "abc",
                            .resolved = {.x = 20,
                                         .y = 20,
                                         .width = 140,
                                         .height = 32},
                            .backgroundColor = {.r = 0.1F,
                                                .g = 0.1F,
                                                .b = 0.1F,
                                                .a = 1.0F},
                            .fontSize = 16}};
  editingDocument.nodes.front().textEdit = {
      .caret = 3, .anchor = 0, .initialized = true};
  assert(canvas.begin(0, 320, 180, 0, error));
  assert(renderer.draw(editingDocument, 320, 180));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads >= 6);
  static_cast<void>(graphics.endFrame());

  CapturingRenderCommands capturingCommands;
  Canvas2D capturedCanvas(*resources, capturingCommands);
  assert(capturedCanvas.initialize(error));
  ui::UiDocument positionOnlyText;
  positionOnlyText.canvasSize = {320, 180};
  positionOnlyText.nodes = {{.id = "legacy_label",
                             .type = "label",
                             .text = "VISIBLE",
                             .resolved = {.x = 28, .y = 26},
                             .fontSize = 18}};
  assert(capturedCanvas.begin(0, 320, 180, 0, error));
  UiCanvasRenderer capturedRenderer(capturedCanvas, font);
  assert(capturedRenderer.draw(positionOnlyText, 320, 180));
  assert(capturedCanvas.flush(error));
  assert(!capturingCommands.scissors.empty());
  for (const ScissorRect submitted : capturingCommands.scissors)
    assert(submitted.x == 28 && submitted.y == 26 && submitted.width > 1 &&
           submitted.height > 1);
  capturedCanvas.shutdown();

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
