#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace demi {
struct AssetManifest;
struct AssetRegistry;
} // namespace demi

namespace demi::assets {

struct ModelLodGenerationRequest {
  std::filesystem::path projectDirectory;
  std::filesystem::path modelManifestPath;
  float mediumRatio = .5F;
  float lowRatio = .2F;
  float maxError = .01F;
};

struct GeneratedModelLod {
  std::string level;
  std::string id;
  std::filesystem::path manifestPath;
  std::size_t sourceTriangles = 0;
  std::size_t triangles = 0;
  float achievedError = 0;
};

struct ModelLodGenerationResult {
  std::string sourceModelId;
  std::filesystem::path recipePath;
  std::vector<GeneratedModelLod> variants;
  Diagnostics diagnostics;
};

// Generates ordinary Model3D assets; runtime LOD selection uses its existing
// medium_lod_model/low_lod_model references and authored distance thresholds.
[[nodiscard]] ModelLodGenerationResult
generateModelLods(const ModelLodGenerationRequest &request);

[[nodiscard]] Diagnostics
validateModelLodSettings(const nlohmann::json &settings,
                         const std::filesystem::path &path);

// Build/cook preparation restores missing or stale recipe outputs. Validation
// remains read-only and can identify the owning recipe for a missing variant.
[[nodiscard]] Diagnostics
ensureModelLods(const std::filesystem::path &projectDirectory);
[[nodiscard]] const AssetManifest *modelLodSource(const AssetRegistry &registry,
                                                  std::string_view generatedId);

} // namespace demi::assets
