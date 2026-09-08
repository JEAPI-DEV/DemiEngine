#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace demi::runtime::composition {

struct ExpansionResult {
  std::optional<nlohmann::json> document;
  Diagnostics diagnostics;
};

// Maps an expanded entity id such as "player/body" back to the scene-owned
// prefab instance and its prefab-local entity id. The longest matching scene
// instance prefix wins so instance ids may themselves contain '/'.
struct PrefabEntityOrigin {
  std::string instanceId;
  std::string localEntityId;
};

[[nodiscard]] std::optional<PrefabEntityOrigin>
prefabEntityOrigin(const nlohmann::json &ownerDocument,
                   std::string_view expandedEntityId);

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
expandPrefabInstance(const std::filesystem::path &ownerPath,
                     const nlohmann::json &instance);

[[nodiscard]] ExpansionResult
inspectPrefab(const std::filesystem::path &prefabPath);

} // namespace demi::runtime::composition
