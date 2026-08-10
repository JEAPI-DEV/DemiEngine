#pragma once

#include "demi/runtime/render/backend/Canvas2D.h"
#include "demi/runtime/ui/TextShaper.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime::render {

struct TextMetrics2D {
  float width = 0.0F;
  float height = 0.0F;
  std::uint32_t lines = 0;
};

class FontAtlas2D {
public:
  explicit FontAtlas2D(GpuResources &resources);
  ~FontAtlas2D();

  FontAtlas2D(const FontAtlas2D &) = delete;
  FontAtlas2D &operator=(const FontAtlas2D &) = delete;

  [[nodiscard]] bool initialize(std::span<const std::byte> ttfData,
                                float pixelHeight, std::string &error);
  [[nodiscard]] bool initializeDefault(float pixelHeight, std::string &error);
  [[nodiscard]] bool initializeBuiltin(float pixelHeight, std::string &error);
  [[nodiscard]] bool addFallback(std::string id,
                                 std::span<const std::byte> ttfData,
                                 std::uint64_t revision, std::string &error);
  void shutdown();

  [[nodiscard]] TextMetrics2D measure(std::string_view text,
                                      float scale = 1.0F) const;
  [[nodiscard]] ui::TextShapeResult shape(std::string_view text,
                                          float scale = 1.0F,
                                          ui::TextDirection direction =
                                              ui::TextDirection::Auto,
                                          std::string_view locale = {}) const;
  [[nodiscard]] bool precache(
      std::string_view text, std::string &error,
      ui::TextDirection direction = ui::TextDirection::Auto,
      std::string_view locale = {}) const;
  [[nodiscard]] bool draw(Canvas2D &canvas, std::string_view text, float x,
                          float y, std::uint32_t rgba, float scale = 1.0F,
                          ScissorRect scissor = {}) const;
  [[nodiscard]] bool draw(Canvas2D &canvas,
                          const ui::TextShapeResult &shaped, float x, float y,
                          std::uint32_t rgba, float scale = 1.0F,
                          ScissorRect scissor = {}) const;
  [[nodiscard]] TextureHandle texture() const;
  [[nodiscard]] std::size_t pageCount() const { return pages_.size(); }
  [[nodiscard]] std::size_t glyphCount() const { return glyphs_.size(); }
  [[nodiscard]] std::size_t maxPages() const { return maxPages_; }
  [[nodiscard]] bool setMaxPages(std::size_t value, std::string &error);
  [[nodiscard]] const ui::FontResolver &fonts() const { return fonts_; }

private:
  struct Glyph {
    std::size_t page = 0;
    float x0 = 0.0F;
    float y0 = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    float xOffset = 0.0F;
    float yOffset = 0.0F;
    float advance = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
  };
  struct Page {
    TextureHandle texture;
    std::uint16_t cursorX = 1;
    std::uint16_t cursorY = 1;
    std::uint16_t rowHeight = 0;
  };

  [[nodiscard]] bool initializeResolver(std::string id,
                                        std::span<const std::byte> ttfData,
                                        float pixelHeight, bool pixelated,
                                        std::string &error);
  [[nodiscard]] bool createPage(std::string &error) const;
  [[nodiscard]] const Glyph *ensureGlyph(std::size_t fontIndex,
                                         std::uint32_t glyphId,
                                         std::string &error) const;
  [[nodiscard]] static std::uint64_t glyphKey(std::size_t fontIndex,
                                              std::uint32_t glyphId);

  GpuResources &resources_;
  ui::FontResolver fonts_;
  ui::TextShaper shaper_;
  mutable std::vector<Page> pages_;
  mutable std::unordered_map<std::uint64_t, Glyph> glyphs_;
  float pixelHeight_ = 0.0F;
  bool pixelated_ = false;
  std::size_t maxPages_ = 8;
};

} // namespace demi::runtime::render
