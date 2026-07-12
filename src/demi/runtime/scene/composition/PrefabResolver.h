#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string_view>

namespace demi::runtime::composition {

struct ExpansionResult {
  std::optional<nlohmann::json> document;
  Diagnostics diagnostics;
};

[[nodiscard]] std::optional<std::filesystem::path>
resolvePrefabReference(const std::filesystem::path &sourcePath,
                       std::string_view reference);

// Objects merge recursively, arrays replace, and null removes inherited data.
[[nodiscard]] nlohmann::json mergeOverride(nlohmann::json inherited,
                                           const nlohmann::json &overrideValue);

[[nodiscard]] ExpansionResult
expandScene(const std::filesystem::path &scenePath,
            const nlohmann::json &sceneDocument);

[[nodiscard]] ExpansionResult
inspectPrefab(const std::filesystem::path &prefabPath);

} // namespace demi::runtime::composition
