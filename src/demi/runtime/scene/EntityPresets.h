#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace demi::runtime::scene_loading {

// Shared, data-only expansion for runtime loading, validation and authoring.
[[nodiscard]] nlohmann::json expandEntityPreset(const nlohmann::json &entity);
[[nodiscard]] inline std::vector<std::string> knownEntityPresets() {
  return {"static_box_3d", "trigger_sphere_3d", "prop_2d", "character_3d"};
}

} // namespace demi::runtime::scene_loading
