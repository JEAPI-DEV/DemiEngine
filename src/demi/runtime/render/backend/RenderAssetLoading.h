#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace demi::runtime::render {

[[nodiscard]] std::vector<std::byte>
readRenderAssetBytes(const std::filesystem::path &path);

[[nodiscard]] TextureSampling2D
textureSampling2D(const AssetManifest &asset,
                  TextureFilter defaultFilter = TextureFilter::Linear);

} // namespace demi::runtime::render
