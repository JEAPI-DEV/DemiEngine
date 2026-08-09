#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace demi::runtime::ui {

enum class TextDirection { Auto, LeftToRight, RightToLeft };

struct TextFontFace {
  std::string id;
  std::shared_ptr<const std::vector<std::byte>> data;
  std::uint64_t revision = 0;
};

struct MissingGlyphDiagnostic {
  std::uint32_t codepoint = 0;
  std::size_t byteOffset = 0;
};

class FontResolver {
public:
  [[nodiscard]] bool add(std::string id, std::span<const std::byte> data,
                         std::uint64_t revision, std::string &error);
  void clear();
  [[nodiscard]] const TextFontFace *font(std::size_t index) const;
  [[nodiscard]] const TextFontFace *resolve(std::uint32_t codepoint) const;
  [[nodiscard]] const TextFontFace *
  resolve(std::span<const std::uint32_t> codepoints) const;
  [[nodiscard]] std::size_t size() const { return fonts_.size(); }
  [[nodiscard]] std::uint64_t revision() const;
  [[nodiscard]] std::size_t coverageCacheSize() const {
    return coverageCache_.size();
  }
  [[nodiscard]] static constexpr std::size_t coverageCacheLimit() {
    return 8192;
  }

private:
  std::vector<TextFontFace> fonts_;
  mutable std::unordered_map<std::uint32_t, std::size_t> coverageCache_;
};

struct ShapedGlyph {
  std::size_t fontIndex = 0;
  std::uint32_t glyphId = 0;
  std::size_t byteOffset = 0;
  float xAdvance = 0.0F;
  float yAdvance = 0.0F;
  float xOffset = 0.0F;
  float yOffset = 0.0F;
};

struct ShapedRun {
  std::size_t byteOffset = 0;
  std::size_t byteLength = 0;
  TextDirection direction = TextDirection::LeftToRight;
  std::vector<ShapedGlyph> glyphs;
  float advance = 0.0F;
};

struct TextShapeRequest {
  std::string_view text;
  float fontSize = 20.0F;
  TextDirection direction = TextDirection::Auto;
  std::string_view locale;
};

struct TextShapeResult {
  std::vector<ShapedRun> runs;
  std::vector<MissingGlyphDiagnostic> missingGlyphs;
  float advance = 0.0F;
  float fontSize = 0.0F;
  bool validUtf8 = true;
  bool complete = true;
};

class TextShaper {
public:
  [[nodiscard]] TextShapeResult shape(const TextShapeRequest &request,
                                      const FontResolver &fonts) const;
};

} // namespace demi::runtime::ui
