#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <string>

namespace demi::assets {

struct ColliderAssetGenerationRequest {
  std::filesystem::path projectDirectory;
  std::filesystem::path modelManifestPath;
  std::string id;
};

struct ColliderAssetGenerationResult {
  std::filesystem::path manifestPath;
  Diagnostics diagnostics;
};

// Generates a deterministic box collider asset from the transformed POSITION
// accessor bounds of a glTF Model3D asset.
[[nodiscard]] ColliderAssetGenerationResult
generateColliderAsset(const ColliderAssetGenerationRequest &request);

} // namespace demi::assets
