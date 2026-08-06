#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

namespace demi::runtime::ui {

struct UiPrefabExpansionResult {
  std::optional<nlohmann::json> document;
  Diagnostics diagnostics;
};

[[nodiscard]] std::optional<std::filesystem::path>
resolveUiPrefabReference(const std::filesystem::path &sourcePath,
                         std::string_view reference);

// Expands every { id, prefab, arguments } node in a HUD root. Expansion is
// transactional: any invalid parameter, duplicate id, missing file, or cycle
// rejects the candidate document.
[[nodiscard]] UiPrefabExpansionResult
expandUiDocument(const std::filesystem::path &hudPath,
                 const nlohmann::json &hudDocument);

[[nodiscard]] UiPrefabExpansionResult
inspectUiPrefab(const std::filesystem::path &prefabPath);

} // namespace demi::runtime::ui
