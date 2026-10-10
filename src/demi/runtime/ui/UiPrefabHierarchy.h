#pragma once
#include "demi/diagnostics/Diagnostic.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <unordered_set>
namespace demi::runtime::ui {
[[nodiscard]] bool composeUiHierarchy(
    nlohmann::json &root, const std::unordered_set<std::string> &removed,
    const std::unordered_set<std::string> &externalParents,
    const std::filesystem::path &source, Diagnostics &diagnostics);
}
