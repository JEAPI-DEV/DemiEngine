#pragma once

#include <array>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace demi::assets {

struct ColliderShapePart {
  std::string id;
  std::vector<std::array<float, 3>> points;
};

struct ColliderShapeAsset {
  std::vector<std::array<float, 3>> points;
  std::array<float, 3> minimum{};
  std::array<float, 3> maximum{};
  std::vector<ColliderShapePart> parts;
};

// Self-contained .collider.json source; no model or backend handles required.
[[nodiscard]] std::optional<ColliderShapeAsset>
parseColliderShapeAsset(const nlohmann::json &document, std::string &error);
[[nodiscard]] std::optional<ColliderShapeAsset>
loadColliderShapeAsset(const std::filesystem::path &path, std::string &error);

} // namespace demi::assets
