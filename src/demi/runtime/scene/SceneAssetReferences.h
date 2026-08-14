#pragma once

#include "demi/runtime/scene/model/World.h"

#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {

// Returns stable asset IDs referenced by the expanded runtime scene. Prefab
// content is included because collection happens after scene expansion.
[[nodiscard]] std::vector<std::string>
collectSceneAssetReferences(const World &world,
                            std::string_view sceneOwner = {});

} // namespace demi::runtime
