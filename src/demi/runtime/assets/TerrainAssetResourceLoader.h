#pragma once

#include "demi/assets/AssetGroup.h"

namespace demi::runtime {
std::shared_ptr<assets::AssetResourceLoader>
createTerrainAssetResourceLoader(const AssetRegistry &registry);
}
