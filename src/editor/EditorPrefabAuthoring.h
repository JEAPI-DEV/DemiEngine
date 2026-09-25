#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace demi::editor {

// Copies one authored scene subtree into a standalone entity-prefab document.
// Expanded runtime data is never baked into the result.
[[nodiscard]] std::optional<nlohmann::json>
makeEntityPrefab(const nlohmann::json &scene, std::string_view selectedId,
                 std::string_view prefabId, std::string &error);

} // namespace demi::editor
