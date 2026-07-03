#pragma once

#include "demi/runtime/scene/SceneData.h"

#include <filesystem>
#include <string>

namespace demi::runtime::scene_loading {

void loadHudFile(World& world, const std::filesystem::path& hudPath, std::string& error);

} // namespace demi::runtime::scene_loading
