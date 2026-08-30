#pragma once

#include "demi/runtime/scene/model/World.h"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>

namespace demi::runtime::scene_loading {

[[nodiscard]] std::optional<ui::UiDocument>
parseHudDocument(const std::filesystem::path &hudPath,
                 const nlohmann::json &document, std::string &error);

void loadHudFile(World &world, const std::filesystem::path &hudPath,
                 std::string &error);

} // namespace demi::runtime::scene_loading
