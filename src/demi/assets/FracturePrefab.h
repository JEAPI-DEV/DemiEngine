#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
namespace demi::assets {
// Internal geometry compilation for FractureAuthoring. Inputs are expanded
// assembly-local entities and normalized component settings, not a prefab
// document or a public recipe format. Throws actionable errors.
nlohmann::json compileFracturePrefab(const std::filesystem::path &project,
                                     const nlohmann::json &entities,
                                     const nlohmann::json &settings);
} // namespace demi::assets
