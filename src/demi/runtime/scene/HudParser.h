#pragma once

#include "demi/runtime/scene/model/World.h"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <unordered_set>

namespace demi::runtime::ui {
struct UiPrefabExpansionResult;
}

namespace demi::runtime::scene_loading {

[[nodiscard]] std::optional<ui::UiDocument>
parseHudDocument(const std::filesystem::path &hudPath,
                 const nlohmann::json &document, std::string &error,
                 ui::UiPrefabExpansionResult *composition = nullptr,
                 const std::unordered_set<std::string> &externalParents = {});

void loadHudFile(World &world, const std::filesystem::path &hudPath,
                 std::string &error);

} // namespace demi::runtime::scene_loading
