#include "demi/runtime/ui/FontRasterizer.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include <cmath>
#include <limits>

namespace demi::runtime::ui {
struct FontRasterizer::Impl {
  FT_Library library = nullptr;
  FT_Face face = nullptr;
  std::vector<std::byte> bytes;
  std::vector<FontAxis> axes;
  float height = 0;
  ~Impl() {
    if (face)
      FT_Done_Face(face);
    if (library)
      FT_Done_FreeType(library);
  }
  bool size(float value, std::string &error) {
    if (!face || !std::isfinite(value) || value <= 0 ||
        double(value) * 64 > std::numeric_limits<FT_Long>::max()) {
      error = "Invalid font raster size";
      return false;
    }
    if (height == value)
      return true;
    FT_Size_RequestRec request{};
    request.type = FT_SIZE_REQUEST_TYPE_REAL_DIM;
    request.height = static_cast<FT_Long>(std::lround(value * 64.0));
    if (FT_Request_Size(face, &request)) {
      error = "FreeType could not size the font";
      return false;
    }
    height = value;
    return true;
  }
};
FontRasterizer::FontRasterizer() = default;
FontRasterizer::~FontRasterizer() = default;
bool FontRasterizer::open(std::span<const std::byte> bytes,
                          const FontVariations &variations,
                          std::string &error) {
  auto candidate = std::make_unique<Impl>();
  if (bytes.empty() ||
      bytes.size() >
          static_cast<std::size_t>(std::numeric_limits<FT_Long>::max())) {
    error = "Empty or oversized font data";
    return false;
  }
  candidate->bytes.assign(bytes.begin(), bytes.end());
  if (FT_Init_FreeType(&candidate->library) ||
      FT_New_Memory_Face(
          candidate->library,
          reinterpret_cast<const FT_Byte *>(candidate->bytes.data()),
          static_cast<FT_Long>(bytes.size()), 0, &candidate->face)) {
    error = "FreeType could not open the font";
    return false;
  }
  FT_MM_Var *description = nullptr;
  if (!FT_Get_MM_Var(candidate->face, &description)) {
    for (FT_UInt i = 0; i < description->num_axis; ++i) {
      const auto &a = description->axis[i];
      std::string tag(4, ' ');
      for (int j = 0; j < 4; ++j)
        tag[j] = static_cast<char>((a.tag >> (24 - j * 8)) & 255);
      candidate->axes.push_back(
          {tag, a.minimum / 65536.F, a.def / 65536.F, a.maximum / 65536.F});
    }
    FT_Done_MM_Var(candidate->library, description);
  }
  std::vector<FT_Fixed> coordinates;
  for (const auto &axis : candidate->axes)
    coordinates.push_back(
        static_cast<FT_Fixed>(std::lround(axis.defaultValue * 65536.0)));
  for (const auto &[tag, value] : variations) {
    auto index = candidate->axes.size();
    for (std::size_t i = 0; i < candidate->axes.size(); ++i)
      if (candidate->axes[i].tag == tag) {
        index = i;
        break;
      }
    if (index == candidate->axes.size() || !std::isfinite(value) ||
        value < candidate->axes[index].minimum ||
        value > candidate->axes[index].maximum) {
      error = "Unsupported or out-of-range font variation axis: " + tag;
      return false;
    }
    coordinates[index] = static_cast<FT_Fixed>(std::lround(value * 65536.0));
  }
  if (!coordinates.empty() &&
      FT_Set_Var_Design_Coordinates(candidate->face,
                                    static_cast<FT_UInt>(coordinates.size()),
                                    coordinates.data())) {
    error = "FreeType could not apply font variation coordinates";
    return false;
  }
  impl_ = std::move(candidate);
  error.clear();
  return true;
}
std::uint32_t FontRasterizer::glyphIndex(std::uint32_t cp) const {
  return impl_ ? FT_Get_Char_Index(impl_->face, cp) : 0;
}
const std::vector<FontAxis> &FontRasterizer::axes() const {
  static const std::vector<FontAxis> empty;
  return impl_ ? impl_->axes : empty;
}
bool FontRasterizer::metrics(float height, float &ascent, float &descent,
                             std::string &error) {
  if (!impl_ || !impl_->size(height, error))
    return false;
  ascent =
      FT_MulFix(impl_->face->ascender, impl_->face->size->metrics.y_scale) /
      64.F;
  descent =
      FT_MulFix(impl_->face->descender, impl_->face->size->metrics.y_scale) /
      64.F;
  return true;
}
bool FontRasterizer::rasterize(std::uint32_t glyph, float height,
                               RasterizedGlyph &output, std::string &error) {
  if (!impl_ || !impl_->size(height, error))
    return false;
  if (glyph >= static_cast<std::uint32_t>(impl_->face->num_glyphs) ||
      FT_Load_Glyph(impl_->face, glyph,
                    FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) ||
      FT_Render_Glyph(impl_->face->glyph, FT_RENDER_MODE_NORMAL)) {
    error = "FreeType glyph rasterization failed";
    return false;
  }
  const auto slot = impl_->face->glyph;
  const auto &bitmap = slot->bitmap;
  if (bitmap.width && bitmap.rows && bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
    error = "Expected grayscale font coverage";
    return false;
  }
  RasterizedGlyph result{.width = static_cast<int>(bitmap.width),
                         .height = static_cast<int>(bitmap.rows),
                         .left = slot->bitmap_left,
                         .top = -slot->bitmap_top,
                         .advance = slot->advance.x / 64.F};
  result.coverage.resize(static_cast<std::size_t>(result.width) *
                         result.height);
  for (int y = 0; y < result.height; ++y) {
    const auto *row =
        bitmap.buffer + (bitmap.pitch >= 0 ? y : result.height - 1 - y) *
                            std::abs(bitmap.pitch);
    for (int x = 0; x < result.width; ++x)
      result.coverage[static_cast<std::size_t>(y) * result.width + x] = row[x];
  }
  output = std::move(result);
  error.clear();
  return true;
}
} // namespace demi::runtime::ui
