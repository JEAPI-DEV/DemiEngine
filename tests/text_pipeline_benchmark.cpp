#include "demi/runtime/render/backend/DefaultFont.h"
#include "demi/runtime/ui/TextLayoutEngine.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace demi::runtime;
int main() {
  ui::FontResolver fonts;
  std::string error;
  if (!fonts.add("font", render::defaultFontData(), 1, error, false,
                 render::defaultFontVariations())) {
    std::cerr << error << '\n';
    return 1;
  }
  ui::TextLayoutRequest request;
  request.text = "A retained interface should shape text with its font "
                 "library, preserve kerning and ligatures, and avoid "
                 "rebuilding font faces for every character.";
  request.width = 400;
  request.height = 160;
  request.fontSize = 20;
  request.font = "font";
  const auto shape = [&](std::string_view text) {
    return ui::TextShaper{}.shape(
        {text, request.fontSize, ui::TextDirection::Auto, {}, request.font},
        fonts);
  };
  const auto measure = [&](std::string_view text) {
    return shape(text).advance;
  };
  std::vector<double> samples;
  std::uint64_t fingerprint = 1469598103934665603ULL;
  const auto mix = [&](std::uint32_t value) {
    fingerprint = (fingerprint ^ value) * 1099511628211ULL;
  };
  for (int iteration = 0; iteration < 220; ++iteration) {
    const auto start = std::chrono::steady_clock::now();
    const auto layout = ui::TextLayoutEngine{}.layout(request, measure, shape);
    const double milliseconds = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
    if (iteration >= 20) {
      samples.push_back(milliseconds);
    }
    for (const auto &line : layout.lines) {
      mix(static_cast<std::uint32_t>(line.graphemeCount));
      mix(std::bit_cast<std::uint32_t>(line.width));
      for (const auto &run : line.shaped.runs) {
        for (const auto &glyph : run.glyphs) {
          mix(glyph.glyphId);
          mix(std::bit_cast<std::uint32_t>(glyph.xAdvance));
          mix(std::bit_cast<std::uint32_t>(glyph.xOffset));
        }
      }
    }
  }
  std::sort(samples.begin(), samples.end());
  std::cout << "layout_median_ms=" << samples[samples.size() / 2]
            << " layout_p95_ms=" << samples[samples.size() * 95 / 100]
            << " fingerprint=" << fingerprint << '\n';
}
