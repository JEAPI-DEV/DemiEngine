#pragma once
#include <nlohmann/json.hpp>
#include <set>
#include <string>
namespace demi::assets {
bool hasMasonryAuthoring(const nlohmann::json &document);
nlohmann::json masonryIntactVisual(const nlohmann::json &entity);
nlohmann::json buildIntactMasonryModelBatches(const nlohmann::json &entity);
nlohmann::json expandMasonry(const nlohmann::json &entities, bool preview,
                             std::set<std::string> *generatedIds = nullptr);
} // namespace demi::assets
