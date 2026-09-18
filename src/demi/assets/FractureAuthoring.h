#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
namespace demi::assets {
// Input/output are flat entity documents. Source entities retain their IDs and
// non-rendering behavior; derived visuals/physics are emitted only for runtime.
nlohmann::json compileEntityFractures(const std::filesystem::path &project,
                                      const nlohmann::json &entities,
                                      const std::string &instancePrefix = {});
bool hasFractureAuthoring(const nlohmann::json &document);
} // namespace demi::assets
