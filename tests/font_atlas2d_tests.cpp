#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/FontAtlas2D.h"

#include <cassert>
#include <cmath>
#include <string>

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

  assert(!font.initialize({}, 32.0F, error));
  assert(font.initializeBuiltin(32.0F, error));
  assert(font.texture());
  const TextMetrics2D oneLine = font.measure("Hello", 1.0F);
  const TextMetrics2D twoLines = font.measure("Hello\nworld", 2.0F);
  assert(oneLine.width > 0.0F);
  assert(oneLine.lines == 1);
  assert(twoLines.lines == 2);
  assert(std::abs(twoLines.height - 128.0F) < 0.01F);
  assert(font.measure("", 1.0F).lines == 0);
  assert(std::abs(font.measure("\xc3\xa9").width -
                  font.measure("?").width) < 0.01F);
  assert(std::abs(font.measure("\xf0\x9f\x8e\xae").width -
                  font.measure("?").width) < 0.01F);
  assert(font.measure("\xc3").width > 0.0F);

  assert(canvas.initialize(error));
  assert(canvas.begin(0, 320, 180, 0, error));
  assert(font.draw(canvas, "ASCII and UTF-8: \xc3\xa9", 4.0F, 36.0F,
                   0xffffffffU));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads > 0);
  static_cast<void>(graphics.endFrame());

  font.shutdown();
  canvas.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
