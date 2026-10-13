#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime::ui {

struct UiPrefabNodeOrigin {
  std::string instanceId;
  std::string localNodeId; // Empty for the instance root.
  std::filesystem::path sourcePath;
  std::string instancePointer;
  std::string definitionPointer;
};

struct UiAuthoredNodeSource {
  std::string pointer;
};

struct UiPrefabExpansionResult {
  std::optional<nlohmann::json> document;
  Diagnostics diagnostics;
  std::unordered_map<std::string, UiPrefabNodeOrigin> origins;
  std::unordered_map<std::string, nlohmann::json> instanceArguments;
  std::unordered_map<std::string, UiAuthoredNodeSource> authoredNodes;
  std::unordered_set<std::string> reservedIds;
  std::unordered_set<std::string> removedIds;
  std::unordered_map<std::string, UiPrefabNodeOrigin> removedOrigins;
  std::unordered_map<std::string, UiAuthoredNodeSource> removedSources;
  std::unordered_map<std::string, nlohmann::json> authoringProperties;
};

[[nodiscard]] std::optional<std::filesystem::path>
resolveUiPrefabReference(const std::filesystem::path &sourcePath,
                         std::string_view reference);

// Expands every { id, prefab, arguments } node in a HUD root. Expansion is
// transactional: any invalid parameter, duplicate id, missing file, or cycle
// rejects the candidate document.
[[nodiscard]] UiPrefabExpansionResult
expandUiDocument(const std::filesystem::path &hudPath,
                 const nlohmann::json &hudDocument,
                 const std::unordered_set<std::string> &externalParents = {});

[[nodiscard]] UiPrefabExpansionResult
inspectUiPrefab(const std::filesystem::path &prefabPath);

} // namespace demi::runtime::ui
