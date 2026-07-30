#include "demi/runtime/render/backend/FontAtlas2D.h"

#include "demi/runtime/render/DefaultPixelFont.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace demi::runtime::render {
namespace {

constexpr int FirstGlyph = 32;
constexpr int GlyphCount = 95;
constexpr int ScalableAtlasSize = 512;
constexpr unsigned char PixelAlphaThreshold = 80;

unsigned char displayGlyph(const unsigned char value) {
  return value >= FirstGlyph && value < FirstGlyph + GlyphCount ? value : '?';
}

unsigned char nextDisplayGlyph(const std::string_view text,
                               std::size_t &offset) {
  const auto first = static_cast<unsigned char>(text[offset++]);
  if (first < 0x80U)
    return first == '\n' ? first : displayGlyph(first);

  std::size_t continuationCount = 0;
  if ((first & 0xe0U) == 0xc0U)
    continuationCount = 1;
  else if ((first & 0xf0U) == 0xe0U)
    continuationCount = 2;
  else if ((first & 0xf8U) == 0xf0U)
    continuationCount = 3;
  else
    return '?';

  if (offset + continuationCount > text.size())
    return '?';
  for (std::size_t index = 0; index < continuationCount; ++index) {
    if ((static_cast<unsigned char>(text[offset + index]) & 0xc0U) != 0x80U)
      return '?';
  }
  offset += continuationCount;
  return '?'; // The built-in atlas currently maps non-ASCII codepoints once.
}

} // namespace

FontAtlas2D::FontAtlas2D(GpuResources &resources) : resources_(resources) {}

FontAtlas2D::~FontAtlas2D() { shutdown(); }

bool FontAtlas2D::initialize(const std::span<const std::byte> ttfData,
                             const float pixelHeight, std::string &error) {
  return initializeBaked(ttfData, pixelHeight, pixelHeight, ScalableAtlasSize,
                         false, error);
}

bool FontAtlas2D::initializeBaked(const std::span<const std::byte> ttfData,
                                  const float rasterHeight,
                                  const float logicalHeight,
                                  const int atlasSize, const bool pixelated,
                                  std::string &error) {
  if (ttfData.empty() || !std::isfinite(rasterHeight) ||
      !std::isfinite(logicalHeight) || rasterHeight <= 0.0F ||
      logicalHeight <= 0.0F || atlasSize <= 0) {
    error = "Font atlas requires TTF data and a positive pixel height.";
    return false;
  }
  shutdown();

  std::vector<unsigned char> alpha(static_cast<std::size_t>(atlasSize) *
                                   atlasSize);
  std::array<stbtt_bakedchar, GlyphCount> baked{};
  const int bottom = stbtt_BakeFontBitmap(
      reinterpret_cast<const unsigned char *>(ttfData.data()), 0, rasterHeight,
      alpha.data(), atlasSize, atlasSize, FirstGlyph, GlyphCount, baked.data());
  if (bottom <= 0) {
    error = "The font glyphs do not fit in the atlas.";
    return false;
  }

  std::vector<std::byte> rgba(alpha.size() * 4U);
  for (std::size_t index = 0; index < alpha.size(); ++index) {
    const unsigned char coverage =
        pixelated ? (alpha[index] >= PixelAlphaThreshold ? 0xffU : 0U)
                  : alpha[index];
    rgba[index * 4U] = std::byte{0xff};
    rgba[index * 4U + 1U] = std::byte{0xff};
    rgba[index * 4U + 2U] = std::byte{0xff};
    rgba[index * 4U + 3U] = static_cast<std::byte>(coverage);
  }
  texture_ = resources_.createTexture(
      TextureCreateInfo{.width = static_cast<std::uint16_t>(atlasSize),
                        .height = static_cast<std::uint16_t>(atlasSize),
                        .format = TextureFormat::RGBA8,
                        .data = rgba,
                        .filter = pixelated ? TextureFilter::Nearest
                                            : TextureFilter::Linear,
                        .debugName = "FontAtlas2D"},
      error);
  if (!texture_)
    return false;

  const float metricScale = logicalHeight / rasterHeight;
  for (int index = 0; index < GlyphCount; ++index) {
    const stbtt_bakedchar &source = baked[index];
    glyphs_[index] = Glyph{
        .x0 = source.x0 / static_cast<float>(atlasSize),
        .y0 = source.y0 / static_cast<float>(atlasSize),
        .x1 = source.x1 / static_cast<float>(atlasSize),
        .y1 = source.y1 / static_cast<float>(atlasSize),
        .xOffset = source.xoff * metricScale,
        .yOffset = source.yoff * metricScale,
        .advance = source.xadvance * metricScale,
    };
  }
  pixelHeight_ = logicalHeight;
  glyphScale_ = metricScale;
  atlasWidth_ = static_cast<std::uint16_t>(atlasSize);
  atlasHeight_ = static_cast<std::uint16_t>(atlasSize);
  return true;
}

bool FontAtlas2D::initializeDefault(const float pixelHeight,
                                    std::string &error) {
  return initializeBaked(std::as_bytes(std::span(DefaultPixelFontData)),
                         pixelHeight, pixelHeight, ScalableAtlasSize, true,
                         error);
}

bool FontAtlas2D::initializeBuiltin(const float pixelHeight,
                                    std::string &error) {
  return initializeDefault(pixelHeight, error);
}

void FontAtlas2D::shutdown() {
  if (texture_)
    resources_.destroy(texture_);
  texture_ = {};
  pixelHeight_ = 0.0F;
  glyphScale_ = 1.0F;
  atlasWidth_ = 0;
  atlasHeight_ = 0;
  std::fill(std::begin(glyphs_), std::end(glyphs_), Glyph{});
}

TextMetrics2D FontAtlas2D::measure(const std::string_view text,
                                   const float scale) const {
  if (!texture_ || scale <= 0.0F || text.empty())
    return {};
  TextMetrics2D result{.height = pixelHeight_ * scale, .lines = 1};
  float lineWidth = 0.0F;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const unsigned char value = nextDisplayGlyph(text, offset);
    if (value == '\n') {
      result.width = std::max(result.width, lineWidth);
      lineWidth = 0.0F;
      ++result.lines;
      result.height += pixelHeight_ * scale;
      continue;
    }
    lineWidth += glyph(displayGlyph(value)).advance * scale;
  }
  result.width = std::max(result.width, lineWidth);
  return result;
}

bool FontAtlas2D::draw(Canvas2D &canvas, const std::string_view text,
                       const float x, const float y, const std::uint32_t rgba,
                       const float scale, const ScissorRect scissor) const {
  if (!texture_ || scale <= 0.0F)
    return false;
  float cursorX = x;
  float cursorY = y;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const unsigned char value = nextDisplayGlyph(text, offset);
    if (value == '\n') {
      cursorX = x;
      cursorY += pixelHeight_ * scale;
      continue;
    }
    const Glyph &item = glyph(displayGlyph(value));
    const float width = (item.x1 - item.x0) * atlasWidth_ * glyphScale_ * scale;
    const float height =
        (item.y1 - item.y0) * atlasHeight_ * glyphScale_ * scale;
    if (width > 0.0F && height > 0.0F &&
        !canvas.image(
            texture_,
            Rect2D{.x = cursorX + item.xOffset * scale,
                   .y = cursorY + item.yOffset * scale,
                   .width = width,
                   .height = height},
            TextureRegion2D{
                .u0 = item.x0, .v0 = item.y0, .u1 = item.x1, .v1 = item.y1},
            rgba, BlendMode::Alpha, scissor))
      return false;
    cursorX += item.advance * scale;
  }
  return true;
}

const FontAtlas2D::Glyph &FontAtlas2D::glyph(const unsigned char value) const {
  return glyphs_[displayGlyph(value) - FirstGlyph];
}

} // namespace demi::runtime::render
