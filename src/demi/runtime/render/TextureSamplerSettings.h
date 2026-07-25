#pragma once

#include "demi/assets/AssetRegistry.h"

#include <raylib.h>

namespace demi::runtime::render_detail {

// Applies the portable sampler state shared by 2D and 3D asset renderers.
// Clamp is the default because GLES 2 requires it for non-power-of-two
// textures; manifests may explicitly opt in to repeat or mirrored repeat.
void applyTextureSamplerSettings(Texture2D &texture,
                                 const TextureImporterSettings &settings,
                                 int defaultFilter);

} // namespace demi::runtime::render_detail
