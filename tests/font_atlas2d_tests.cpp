#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/FontAtlas2D.h"
#include "demi/runtime/ui/TextLayoutEngine.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <string>
#include <fstream>
#include <filesystem>
#include <vector>

#include <utf8proc.h>

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

  FontAtlas2D bounded(*resources);
  assert(bounded.setMaxPages(1, error));
  assert(bounded.initializeBuiltin(64.0F, error));
  std::string extendedText;
  for (utf8proc_int32_t codepoint = 0x20; codepoint <= 0x2cff; ++codepoint) {
    utf8proc_uint8_t bytes[4]{};
    const auto size = utf8proc_encode_char(codepoint, bytes);
    if (size > 0)
      extendedText.append(reinterpret_cast<const char *>(bytes),
                          static_cast<std::size_t>(size));
  }
  assert(!bounded.precache(extendedText, error));
  assert(error.find("page budget") != std::string::npos);
  assert(bounded.fonts().coverageCacheSize() <=
         bounded.fonts().coverageCacheLimit());

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
  const auto latin = font.shape("café");
  const auto descenders = font.shape("fpgqy", 0.375F);
  const auto missing = font.shape("\xf0\x9f\x8e\xae");
  assert(latin.validUtf8 && latin.complete && !latin.runs.empty());
  assert(descenders.validUtf8 && descenders.complete &&
         descenders.baseline > 0.0F && descenders.baseline < 12.0F);
  assert(missing.validUtf8 && !missing.complete &&
         missing.missingGlyphs.size() == 1);
  assert(font.measure("\xc3").width == 0.0F);
  assert(!font.shape("\xc3").validUtf8);
  const auto bidi = font.shape("abc \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d");
  assert(bidi.validUtf8 && !bidi.complete && bidi.runs.size() >= 2);
  assert(bidi.runs.front().direction ==
         demi::runtime::ui::TextDirection::LeftToRight);
  assert(bidi.runs.back().direction ==
         demi::runtime::ui::TextDirection::RightToLeft);

  std::ifstream fallbackFile(
      std::filesystem::path(DEMI_SOURCE_DIR) /
          "fonts/Pixelify_Sans/static/PixelifySans-Bold.ttf",
      std::ios::binary);
  std::vector<char> fallbackChars((std::istreambuf_iterator<char>(fallbackFile)),
                                  std::istreambuf_iterator<char>());
  std::vector<std::byte> fallbackBytes(fallbackChars.size());
  std::transform(fallbackChars.begin(), fallbackChars.end(),
                 fallbackBytes.begin(),
                 [](char value) { return static_cast<std::byte>(value); });
  assert(font.addFallback("pixel-bold", fallbackBytes, 7, error));
  assert(!font.addFallback("pixel-bold", fallbackBytes, 8, error));
  assert(font.fonts().size() == 2 && font.fonts().revision() != 0);
  const auto selectedFont = font.shape(
      "Selected", 1.0F, demi::runtime::ui::TextDirection::Auto, {},
      "pixel-bold");
  assert(selectedFont.complete && !selectedFont.runs.empty() &&
         !selectedFont.runs.front().glyphs.empty() &&
         selectedFont.runs.front().glyphs.front().fontIndex == 1);
  const auto missingSelection = font.shape(
      "Fallback", 1.0F, demi::runtime::ui::TextDirection::Auto, {},
      "asset://missing-font");
  assert(missingSelection.complete && !missingSelection.runs.empty() &&
         missingSelection.runs.front().glyphs.front().fontIndex == 0);
  assert(!font.setMaxPages(0, error));
  assert(font.setMaxPages(2, error) && font.maxPages() == 2);

  const auto rtlLayout = demi::runtime::ui::TextLayoutEngine{}.layout(
      {.text = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d",
       .width = 200.0F,
       .fontSize = 32.0F,
       .direction = demi::runtime::ui::TextDirection::RightToLeft},
      [&](const std::string_view value) { return font.measure(value).width; },
      [&](const std::string_view value) {
        return font.shape(value, 1.0F,
                          demi::runtime::ui::TextDirection::RightToLeft);
      });
  assert(rtlLayout.lines.size() == 1 && rtlLayout.carets.size() == 5);
  assert(rtlLayout.carets.front().x > rtlLayout.carets.back().x);
  const auto rtlSelection =
      demi::runtime::ui::TextLayoutEngine::selectionRects(rtlLayout, 0, 4);
  assert(!rtlSelection.empty());
  for (const auto &rect : rtlSelection)
    assert(rect.width > 0.0F);

  assert(canvas.initialize(error));
  assert(canvas.begin(0, 320, 180, 0, error));
  assert(font.draw(canvas, selectedFont, 4.0F, 18.0F, 0xffffffffU));
  assert(font.pageCount() >= 2);
  assert(font.draw(canvas, "ASCII and UTF-8: \xc3\xa9", 4.0F, 36.0F,
                   0xffffffffU));
  assert(font.draw(canvas, font.shape("Scaled", 2.0F), 4.0F, 72.0F,
                   0xffffffffU));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads > 0);
  static_cast<void>(graphics.endFrame());

  bounded.shutdown();
  font.shutdown();
  canvas.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
