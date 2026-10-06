#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace demi::runtime::composition {
// entity_ids maps prefab-local expanded IDs to IDs in the owning document.
// Unmapped entities retain their conventional instance prefix.
using PrefabEntityIdMap = std::unordered_map<std::string, std::string>;
PrefabEntityIdMap prefabEntityIdRemapping(
    const nlohmann::json &mapping, const std::vector<std::string> &expandedIds,
    std::string_view instancePrefix, std::string_view ownerPrefix);
} // namespace demi::runtime::composition
