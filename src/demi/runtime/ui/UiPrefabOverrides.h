#pragma once
#include "demi/diagnostics/Diagnostic.h"
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime::ui {
using UiOverrideChildExpander = std::function<std::optional<nlohmann::json>(
    nlohmann::json, const std::string &, std::size_t)>;
// One target map: $root or a stable local ID. Objects patch properties and
// append local children; null removes an inherited descendant.
[[nodiscard]] bool applyUiPrefabOverrides(
    nlohmann::json &root, const nlohmann::json &instance,
    const std::filesystem::path &source, Diagnostics &diagnostics,
    const UiOverrideChildExpander &expandChild,
    std::unordered_set<std::string> &removed,
    const std::unordered_map<std::string, std::string> &inheritedIds);
} // namespace demi::runtime::ui
