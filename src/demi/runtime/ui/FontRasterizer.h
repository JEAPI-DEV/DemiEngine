#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace demi::runtime::ui {
using FontVariations = std::map<std::string, float>;
struct FontAxis {
  std::string tag;
  float minimum = 0, defaultValue = 0, maximum = 0;
};
struct RasterizedGlyph {
  int width = 0, height = 0, left = 0, top = 0;
  float advance = 0;
  std::vector<unsigned char> coverage;
};
// Owns a face and its font bytes. One instance per immutable variation
// selection; size changes are local to its owning render thread, never shared
// with shaping.
class FontRasterizer {
public:
  FontRasterizer();
  ~FontRasterizer();
  FontRasterizer(const FontRasterizer &) = delete;
  FontRasterizer &operator=(const FontRasterizer &) = delete;
  bool open(std::span<const std::byte>, const FontVariations &,
            std::string &error);
  std::uint32_t glyphIndex(std::uint32_t codepoint) const;
  bool rasterize(std::uint32_t glyph, float height, RasterizedGlyph &,
                 std::string &error);
  bool metrics(float height, float &ascent, float &descent, std::string &error);
  const std::vector<FontAxis> &axes() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace demi::runtime::ui
