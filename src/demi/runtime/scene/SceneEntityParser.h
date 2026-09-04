#pragma once

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/World.h"

#include <filesystem>
#include <string>
#include <vector>

namespace demi::runtime::scene_loading {

[[nodiscard]] Entity parseSceneEntity(const Json &entityJson);
[[nodiscard]] World parseSceneWorld(const std::filesystem::path &scenePath,
                                    const Json &document);

// P5: data-only entity preset names expanded by parseSceneEntity.
[[nodiscard]] inline std::vector<std::string> knownEntityPresets() {
  return {"static_box_3d", "trigger_sphere_3d", "prop_2d", "character_3d"};
}

} // namespace demi::runtime::scene_loading
