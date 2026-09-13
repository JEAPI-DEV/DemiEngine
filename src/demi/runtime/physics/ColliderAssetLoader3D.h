#pragma once
#include "demi/assets/AssetGroup.h"
#include <memory>

namespace demi::runtime {
struct World;
[[nodiscard]] std::shared_ptr<assets::AssetResourceLoader>
createColliderAssetLoader3D(World &world);
} // namespace demi::runtime
