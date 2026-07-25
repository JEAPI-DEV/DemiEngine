#include "demi/runtime/render/TextureSamplerSettings.h"

#include <rlgl.h>

namespace demi::runtime::render_detail {

void applyTextureSamplerSettings(Texture2D &texture,
                                 const TextureImporterSettings &settings,
                                 const int defaultFilter) {
  if (settings.mipmaps && texture.mipmaps <= 1)
    GenTextureMipmaps(&texture);

  int filter = defaultFilter;
  if (settings.filter == "nearest")
    filter = TEXTURE_FILTER_POINT;
  else if (settings.filter == "bilinear")
    filter = TEXTURE_FILTER_BILINEAR;
  else if (settings.filter == "trilinear")
    filter = TEXTURE_FILTER_TRILINEAR;
  SetTextureFilter(texture, filter);

  const int wrap = settings.wrap == "repeat"    ? TEXTURE_WRAP_REPEAT
                   : settings.wrap == "mirror" ? TEXTURE_WRAP_MIRROR_REPEAT
                                                  : TEXTURE_WRAP_CLAMP;
  SetTextureWrap(texture, wrap);
}

} // namespace demi::runtime::render_detail
