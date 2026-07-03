#pragma once

#include "demi/runtime/scene/SceneData.h"
#include "demi/runtime/scene/SceneJson.h"

#include <filesystem>

namespace demi::runtime::scene_loading {

[[nodiscard]] World parseSceneWorld(const std::filesystem::path& scenePath, const Json& document);

} // namespace demi::runtime::scene_loading
