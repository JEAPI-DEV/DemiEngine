#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/model/World.h"

namespace demi::runtime {

void generateTilemapColliders(World &world, const AssetRegistry &registry);

} // namespace demi::runtime
