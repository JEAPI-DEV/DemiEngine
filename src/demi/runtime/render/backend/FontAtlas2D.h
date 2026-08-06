#pragma once

#include "demi/runtime/render/backend/Canvas2D.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

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
  void shutdown();

  [[nodiscard]] TextMetrics2D measure(std::string_view text,
                                      float scale = 1.0F) const;
  [[nodiscard]] bool draw(Canvas2D &canvas, std::string_view text, float x,
                          float y, std::uint32_t rgba, float scale = 1.0F,
                          ScissorRect scissor = {}) const;
  [[nodiscard]] TextureHandle texture() const { return texture_; }

private:
  struct Glyph {
    float x0 = 0.0F;
    float y0 = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    float xOffset = 0.0F;
    float yOffset = 0.0F;
    float advance = 0.0F;
  };

  [[nodiscard]] bool initializeBaked(std::span<const std::byte> ttfData,
                                     float rasterHeight, float logicalHeight,
                                     int atlasSize, bool pixelated,
                                     std::string &error);
  [[nodiscard]] const Glyph &glyph(unsigned char value) const;

  GpuResources &resources_;
  TextureHandle texture_;
  Glyph glyphs_[95]{};
  float pixelHeight_ = 0.0F;
  float glyphScale_ = 1.0F;
  std::uint16_t atlasWidth_ = 0;
  std::uint16_t atlasHeight_ = 0;
};

} // namespace demi::runtime::render
