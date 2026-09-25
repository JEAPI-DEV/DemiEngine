#pragma once
#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/ui/FontRasterizer.h"
namespace demi::assets {
runtime::ui::FontVariations fontAssetVariations(const AssetManifest &);
Diagnostics validateFontAssetSettings(const AssetManifest &);
} // namespace demi::assets
