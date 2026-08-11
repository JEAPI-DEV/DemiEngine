#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace demi::assets {

struct ColliderAssetGenerationRequest {
  std::filesystem::path projectDirectory;
  std::filesystem::path modelManifestPath;
  std::string id;
  float detail = 0.0F;
  std::string body = "static";
  std::filesystem::path previewPath;
};

struct ColliderAssetGenerationResult {
  std::filesystem::path manifestPath;
  Diagnostics diagnostics;
};

// Generates a deterministic box collider asset from the transformed POSITION
// accessor bounds of a glTF Model3D asset.
[[nodiscard]] ColliderAssetGenerationResult
generateColliderAsset(const ColliderAssetGenerationRequest &request);

struct ColliderRecommendation {
  std::string shape;
  std::string body;
  float detail = 0.0F;
  std::string reason;
  nlohmann::json component;
};

[[nodiscard]] std::optional<ColliderRecommendation>
recommendCollider(const std::filesystem::path &modelManifestPath,
                  const std::string &body, Diagnostics &diagnostics);

} // namespace demi::assets
