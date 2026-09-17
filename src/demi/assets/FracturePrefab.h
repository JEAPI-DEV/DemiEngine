#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
namespace demi::assets {
// Pure compilation: source prefab remains untouched. Caller supplies expanded
// prefab-local entities, including their hierarchy. Throws actionable errors.
nlohmann::json compileFracturePrefab(const std::filesystem::path &project,
                                     const nlohmann::json &entities,
                                     const nlohmann::json &settings);
} // namespace demi::assets
