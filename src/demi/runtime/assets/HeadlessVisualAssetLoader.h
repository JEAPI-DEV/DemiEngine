#pragma once

#include "demi/assets/AssetGroup.h"

#include <memory>

namespace demi::runtime {

// Headless/renderer-free hosts verify visual source availability and maintain
// ordinary resource ownership without retaining encoded images/models or GPU
// allocations. Data and text assets remain owned by their existing loaders.
[[nodiscard]] std::shared_ptr<assets::AssetResourceLoader>
createHeadlessVisualAssetLoader(const AssetRegistry &source);

} // namespace demi::runtime
