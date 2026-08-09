#include "demi/runtime/render/backend/FontAtlas2D.h"

#include "demi/runtime/render/DefaultPixelFont.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace demi::runtime::render {
namespace {
constexpr std::uint16_t AtlasSize = 512;
constexpr unsigned char PixelAlphaThreshold = 80;
}

FontAtlas2D::FontAtlas2D(GpuResources &resources) : resources_(resources) {}
FontAtlas2D::~FontAtlas2D() { shutdown(); }

bool FontAtlas2D::initialize(const std::span<const std::byte> ttfData,
                             const float pixelHeight, std::string &error) {
  return initializeResolver("primary", ttfData, pixelHeight, false, error);
}

bool FontAtlas2D::initializeResolver(std::string id,
                                     const std::span<const std::byte> ttfData,
                                     const float pixelHeight,
                                     const bool pixelated,
                                     std::string &error) {
  if (!std::isfinite(pixelHeight) || pixelHeight <= 0.0F) {
    error = "Font atlas requires a positive finite pixel height.";
    return false;
  }
  shutdown();
  if (!fonts_.add(std::move(id), ttfData, 1, error))
    return false;
  pixelHeight_ = pixelHeight;
  pixelated_ = pixelated;
  if (!createPage(error)) {
    shutdown();
    return false;
  }
  // Prime common UI glyphs while retaining demand-driven fallback pages.
  if (!precache(
      " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
      "abcdefghijklmnopqrstuvwxyz{|}~",
      error)) {
    shutdown();
    return false;
  }
  error.clear();
  return true;
}

bool FontAtlas2D::initializeDefault(const float pixelHeight,
                                    std::string &error) {
  return initializeResolver("default-pixel",
                            std::as_bytes(std::span(DefaultPixelFontData)),
                            pixelHeight, true, error);
}

bool FontAtlas2D::initializeBuiltin(const float pixelHeight,
                                    std::string &error) {
  return initializeDefault(pixelHeight, error);
}

bool FontAtlas2D::addFallback(std::string id,
                              const std::span<const std::byte> ttfData,
                              const std::uint64_t revision,
                              std::string &error) {
  return fonts_.add(std::move(id), ttfData, revision, error);
}

bool FontAtlas2D::setMaxPages(const std::size_t value, std::string &error) {
  if (value == 0 || value < pages_.size()) {
    error = "Font atlas page limit must be non-zero and cannot discard live pages.";
    return false;
  }
  maxPages_ = value;
  error.clear();
  return true;
}

void FontAtlas2D::shutdown() {
  for (const auto &page : pages_)
    if (page.texture)
      resources_.destroy(page.texture);
  pages_.clear();
  glyphs_.clear();
  fonts_.clear();
  pixelHeight_ = 0.0F;
  pixelated_ = false;
}

bool FontAtlas2D::createPage(std::string &error) const {
  if (pages_.size() >= maxPages_) {
    error = "Font atlas exhausted its configured page budget (" +
            std::to_string(maxPages_) + ").";
    return false;
  }
  TextureHandle texture = resources_.createTexture(
      {.width = AtlasSize,
       .height = AtlasSize,
       .format = TextureFormat::RGBA8,
       .data = {},
       .filter = pixelated_ ? TextureFilter::Nearest : TextureFilter::Linear,
       .debugName = "FontAtlas2D.page." + std::to_string(pages_.size())},
      error);
  if (!texture)
    return false;
  pages_.push_back({.texture = texture});
  return true;
}

std::uint64_t FontAtlas2D::glyphKey(const std::size_t fontIndex,
                                    const std::uint32_t glyphId) {
  return (static_cast<std::uint64_t>(fontIndex) << 32U) | glyphId;
}

const FontAtlas2D::Glyph *
FontAtlas2D::ensureGlyph(const std::size_t fontIndex,
                         const std::uint32_t glyphId,
                         std::string &error) const {
  const auto key = glyphKey(fontIndex, glyphId);
  if (const auto found = glyphs_.find(key); found != glyphs_.end())
    return &found->second;
  const ui::TextFontFace *face = fonts_.font(fontIndex);
  if (face == nullptr || !face->data || face->data->empty()) {
    error = "Shaped glyph refers to an unavailable fallback font.";
    return nullptr;
  }
  stbtt_fontinfo info{};
  const auto *bytes = reinterpret_cast<const unsigned char *>(face->data->data());
  if (!stbtt_InitFont(&info, bytes, stbtt_GetFontOffsetForIndex(bytes, 0))) {
    error = "Fallback font data became invalid while building its atlas.";
    return nullptr;
  }
  const float rasterScale = stbtt_ScaleForPixelHeight(&info, pixelHeight_);
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  stbtt_GetGlyphBitmapBox(&info, static_cast<int>(glyphId), rasterScale,
                          rasterScale, &x0, &y0, &x1, &y1);
  int advance = 0, bearing = 0;
  stbtt_GetGlyphHMetrics(&info, static_cast<int>(glyphId), &advance, &bearing);
  const std::uint16_t width = static_cast<std::uint16_t>(std::max(x1 - x0, 0));
  const std::uint16_t height = static_cast<std::uint16_t>(std::max(y1 - y0, 0));
  if (width + 2U >= AtlasSize || height + 2U >= AtlasSize) {
    error = "A rasterized font glyph exceeds the atlas page size.";
    return nullptr;
  }
  if (pages_.empty() && !createPage(error))
    return nullptr;
  Page *page = &pages_.back();
  if (page->cursorX + width + 1U >= AtlasSize) {
    page->cursorX = 1;
    page->cursorY = static_cast<std::uint16_t>(page->cursorY +
                                               page->rowHeight + 1U);
    page->rowHeight = 0;
  }
  if (page->cursorY + height + 1U >= AtlasSize) {
    if (!createPage(error))
      return nullptr;
    page = &pages_.back();
  }
  const std::uint16_t atlasX = page->cursorX;
  const std::uint16_t atlasY = page->cursorY;
  page->cursorX = static_cast<std::uint16_t>(page->cursorX + width + 1U);
  page->rowHeight = std::max(page->rowHeight, height);

  if (width > 0 && height > 0) {
    std::vector<unsigned char> alpha(static_cast<std::size_t>(width) * height);
    stbtt_MakeGlyphBitmap(&info, alpha.data(), width, height, width, rasterScale,
                          rasterScale, static_cast<int>(glyphId));
    std::vector<std::byte> rgba(alpha.size() * 4U);
    for (std::size_t index = 0; index < alpha.size(); ++index) {
      const unsigned char coverage =
          pixelated_ ? (alpha[index] >= PixelAlphaThreshold ? 0xffU : 0U)
                     : alpha[index];
      rgba[index * 4U] = std::byte{0xff};
      rgba[index * 4U + 1U] = std::byte{0xff};
      rgba[index * 4U + 2U] = std::byte{0xff};
      rgba[index * 4U + 3U] = static_cast<std::byte>(coverage);
    }
    if (!resources_.updateTexture(
            page->texture,
            {.x = atlasX,
             .y = atlasY,
             .width = width,
             .height = height,
             .data = rgba},
            error))
      return nullptr;
  }
  const auto [inserted, _] = glyphs_.emplace(
      key, Glyph{.page = pages_.size() - 1,
                 .x0 = atlasX / static_cast<float>(AtlasSize),
                 .y0 = atlasY / static_cast<float>(AtlasSize),
                 .x1 = (atlasX + width) / static_cast<float>(AtlasSize),
                 .y1 = (atlasY + height) / static_cast<float>(AtlasSize),
                 .xOffset = static_cast<float>(x0),
                 .yOffset = static_cast<float>(y0),
                 .advance = advance * rasterScale,
                 .width = static_cast<float>(width),
                 .height = static_cast<float>(height)});
  return &inserted->second;
}

ui::TextShapeResult FontAtlas2D::shape(const std::string_view text,
                                       const float scale,
                                       const ui::TextDirection direction,
                                       const std::string_view locale) const {
  return shaper_.shape({.text = text,
                        .fontSize = pixelHeight_ * scale,
                        .direction = direction,
                        .locale = locale},
                       fonts_);
}

bool FontAtlas2D::precache(const std::string_view text, std::string &error,
                           const ui::TextDirection direction,
                           const std::string_view locale) const {
  const auto shaped = shape(text, 1.0F, direction, locale);
  if (!shaped.validUtf8) {
    error = "Font atlas cannot precache invalid UTF-8 text.";
    return false;
  }
  for (const auto &run : shaped.runs)
    for (const auto &glyph : run.glyphs)
      if (ensureGlyph(glyph.fontIndex, glyph.glyphId, error) == nullptr)
        return false;
  error.clear();
  return true;
}

TextMetrics2D FontAtlas2D::measure(const std::string_view text,
                                   const float scale) const {
  if (text.empty() || scale <= 0.0F || fonts_.size() == 0)
    return {};
  TextMetrics2D result{.height = pixelHeight_ * scale, .lines = 1};
  std::size_t begin = 0;
  while (begin <= text.size()) {
    const std::size_t end = text.find('\n', begin);
    const std::string_view line = text.substr(
        begin, end == std::string_view::npos ? text.size() - begin : end - begin);
    result.width = std::max(result.width, shape(line, scale).advance);
    if (end == std::string_view::npos)
      break;
    ++result.lines;
    result.height += pixelHeight_ * scale;
    begin = end + 1;
  }
  return result;
}

bool FontAtlas2D::draw(Canvas2D &canvas, const std::string_view text,
                       const float x, const float y, const std::uint32_t rgba,
                       const float scale, const ScissorRect scissor) const {
  return draw(canvas, shape(text), x, y, rgba, scale, scissor);
}

bool FontAtlas2D::draw(Canvas2D &canvas,
                       const ui::TextShapeResult &shaped, const float x,
                       const float y, const std::uint32_t rgba,
                       const float scale, const ScissorRect scissor) const {
  if (scale <= 0.0F || fonts_.size() == 0)
    return false;
  const float bitmapScale =
      (shaped.fontSize > 0.0F ? shaped.fontSize / pixelHeight_ : 1.0F) * scale;
  float cursorX = x;
  float cursorY = y;
  std::string error;
  for (const auto &run : shaped.runs) {
    for (const auto &source : run.glyphs) {
      const Glyph *glyph = ensureGlyph(source.fontIndex, source.glyphId, error);
      if (glyph == nullptr)
        return false;
      if (glyph->width > 0.0F && glyph->height > 0.0F &&
          !canvas.image(
              pages_[glyph->page].texture,
              {.x = cursorX + source.xOffset * scale + glyph->xOffset * bitmapScale,
               .y = cursorY + source.yOffset * scale + glyph->yOffset * bitmapScale,
               .width = glyph->width * bitmapScale,
               .height = glyph->height * bitmapScale},
              {.u0 = glyph->x0,
               .v0 = glyph->y0,
               .u1 = glyph->x1,
               .v1 = glyph->y1},
              rgba, BlendMode::Alpha, scissor))
        return false;
      cursorX += source.xAdvance * scale;
      cursorY += source.yAdvance * scale;
    }
  }
  return true;
}

TextureHandle FontAtlas2D::texture() const {
  return pages_.empty() ? TextureHandle{} : pages_.front().texture;
}

} // namespace demi::runtime::render
