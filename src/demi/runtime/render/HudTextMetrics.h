#pragma once

#include <algorithm>

namespace demi::runtime {

struct HudTextMetrics {
  float fontSize = 10.0F;
  float letterSpacing = 1.0F;
};

// HUD coordinates scale with the canvas, but text must remain readable in a
// small window. Keep a physical-pixel floor and scale tracking proportionally
// so shrunken glyphs do not retain oversized gaps between letters.
[[nodiscard]] inline HudTextMetrics hudTextMetrics(const float authoredSize,
                                                    const float canvasScale) {
  constexpr float MinimumFontPixels = 10.0F;
  constexpr float FullSizeLetterSpacing = 5.0F;
  constexpr float FullSizeFontPixels = 28.0F;
  const float fontSize =
      std::max(authoredSize * std::max(canvasScale, 0.0F), MinimumFontPixels);
  return {.fontSize = fontSize,
          .letterSpacing = std::clamp(
              fontSize * FullSizeLetterSpacing / FullSizeFontPixels, 1.0F,
              FullSizeLetterSpacing)};
}

} // namespace demi::runtime
