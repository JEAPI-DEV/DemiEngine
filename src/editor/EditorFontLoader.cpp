#include "editor/EditorFontLoader.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace demi::editor {
struct EditorFontLoader::Impl : ImFontLoader {
  runtime::ui::FontVariations variations;
  explicit Impl(runtime::ui::FontVariations axes)
      : variations(std::move(axes)) {
    Name = "Demi FreeType variable fonts";
    FontSrcInit = [](ImFontAtlas *, ImFontConfig *src) {
      auto raster = std::make_unique<runtime::ui::FontRasterizer>();
      std::string error;
      const auto *self = static_cast<const Impl *>(src->FontLoader);
      if (!raster->open({static_cast<const std::byte *>(src->FontData),
                         static_cast<std::size_t>(src->FontDataSize)},
                        self->variations, error))
        return false;
      src->FontLoaderData = raster.release();
      return true;
    };
    FontSrcDestroy = [](ImFontAtlas *, ImFontConfig *src) {
      delete static_cast<runtime::ui::FontRasterizer *>(src->FontLoaderData);
      src->FontLoaderData = nullptr;
    };
    FontSrcContainsGlyph = [](ImFontAtlas *, ImFontConfig *src, ImWchar cp) {
      return static_cast<runtime::ui::FontRasterizer *>(src->FontLoaderData)
                 ->glyphIndex(cp) != 0;
    };
    FontBakedInit = [](ImFontAtlas *, ImFontConfig *src, ImFontBaked *baked,
                       void *) {
      float ascent = 0, descent = 0;
      std::string error;
      const float density = src->RasterizerDensity * baked->RasterizerDensity;
      if (!static_cast<runtime::ui::FontRasterizer *>(src->FontLoaderData)
               ->metrics(size(src, baked) * density, ascent, descent, error))
        return false;
      if (!src->MergeMode) {
        baked->Ascent = ascent / density / src->ExtraSizeScale;
        baked->Descent = descent / density / src->ExtraSizeScale;
      }
      return true;
    };
    FontBakedLoadGlyph = [](ImFontAtlas *atlas, ImFontConfig *src,
                            ImFontBaked *baked, void *, ImWchar cp,
                            ImFontGlyph *out, float *advance) {
      auto *raster =
          static_cast<runtime::ui::FontRasterizer *>(src->FontLoaderData);
      const auto glyph = raster->glyphIndex(cp);
      if (!glyph)
        return false;
      const float density = src->RasterizerDensity * baked->RasterizerDensity;
      runtime::ui::RasterizedGlyph bitmap;
      std::string error;
      if (!raster->rasterize(glyph, size(src, baked) * density, bitmap, error))
        return false;
      if (advance) {
        *advance = bitmap.advance / density;
        return true;
      }
      out->Codepoint = cp;
      out->AdvanceX = bitmap.advance / density;
      if (!bitmap.width || !bitmap.height)
        return true;
      const auto id =
          ImFontAtlasPackAddRect(atlas, bitmap.width, bitmap.height);
      if (id == ImFontAtlasRectId_Invalid)
        return false;
      auto *rect = ImFontAtlasPackGetRect(atlas, id);
      atlas->Builder->TempBuffer.resize(bitmap.width * bitmap.height * 4);
      auto *pixels =
          reinterpret_cast<unsigned char *>(atlas->Builder->TempBuffer.Data);
      for (std::size_t i = 0; i < bitmap.coverage.size(); ++i) {
        pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = bitmap.coverage[i];
      }
      const float reference = baked->OwnerFont->Sources[0]->SizePixels;
      const float offsetScale = reference > 0 ? baked->Size / reference : 1;
      out->X0 = bitmap.left / density + src->GlyphOffset.x * offsetScale;
      out->Y0 = bitmap.top / density + baked->Ascent +
                src->GlyphOffset.y * offsetScale;
      out->X1 = out->X0 + bitmap.width / density;
      out->Y1 = out->Y0 + bitmap.height / density;
      out->Visible = true;
      out->Colored = false;
      out->PackId = id;
      ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out, rect, pixels,
                                         ImTextureFormat_RGBA32,
                                         bitmap.width * 4);
      return true;
    };
  }
  static float size(ImFontConfig *src, ImFontBaked *baked) {
    const float reference = baked->OwnerFont->Sources[0]->SizePixels;
    return baked->Size * src->ExtraSizeScale *
           (src->MergeMode && src->SizePixels > 0 && reference > 0
                ? src->SizePixels / reference
                : 1);
  }
};
EditorFontLoader::EditorFontLoader(runtime::ui::FontVariations variations)
    : impl_(std::make_unique<Impl>(std::move(variations))) {}
EditorFontLoader::~EditorFontLoader() = default;
const ImFontLoader *EditorFontLoader::loader() const { return impl_.get(); }
} // namespace demi::editor
