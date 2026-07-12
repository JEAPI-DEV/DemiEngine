#pragma once

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/ProjectData.h"

#include <filesystem>
#include <optional>
#include <string>

namespace demi::runtime::scene_loading {

[[nodiscard]] std::optional<ProjectData>
parseProjectData(const std::filesystem::path &projectPath, const Json &document,
                 std::string &error);

} // namespace demi::runtime::scene_loading
