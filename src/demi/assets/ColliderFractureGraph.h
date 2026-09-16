#pragma once

#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace demi::assets {

struct ColliderFractureBond {
  std::string id;
  std::string firstPart;
  std::string secondPart;
  float health = 1;
  bool operator==(const ColliderFractureBond &) const = default;
};

struct ColliderFractureGraph {
  std::vector<ColliderFractureBond> bonds;
  std::vector<std::string> anchors;
  bool operator==(const ColliderFractureGraph &) const = default;
};

// Asset-local IDs only. No external dependencies or backend state.
[[nodiscard]] std::optional<ColliderFractureGraph>
parseColliderFractureGraph(const nlohmann::json &document,
                           std::span<const std::string> partIds,
                           std::string &error);

} // namespace demi::assets
