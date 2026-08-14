#pragma once

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/World.h"

#include <filesystem>

namespace demi::runtime::scene_loading {

[[nodiscard]] Entity parseSceneEntity(const Json &entityJson);
[[nodiscard]] World parseSceneWorld(const std::filesystem::path &scenePath,
                                    const Json &document);

} // namespace demi::runtime::scene_loading
