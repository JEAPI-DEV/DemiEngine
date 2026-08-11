#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <array>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>

namespace demi::assets {

// Author-owned conversion from a source model coordinate system into Demi's
// Y-up, +Z-forward, meter-based engine space. The conversion is applied by all
// geometry consumers so rendering, animation, bounds, and colliders agree.
struct ModelImportProfile {
  int formatVersion = 1;
  std::string preset = "static_prop";
  std::string sourceUp = "+y";
  std::string sourceForward = "+z";
  float metersPerUnit = 1.0F;
  std::string rootNode = "preserve";
  std::string materialPolicy = "import";
  bool importAnimations = false;
  bool optimizeMeshes = true;
};

[[nodiscard]] ModelImportProfile modelImportPreset(const std::string &name);
[[nodiscard]] std::optional<ModelImportProfile>
parseModelImportProfile(const nlohmann::json &settings,
                        Diagnostics *diagnostics = nullptr,
                        const std::string &path = {});
[[nodiscard]] std::array<float, 16>
modelImportConversion(const ModelImportProfile &profile);
[[nodiscard]] nlohmann::json
modelImportProfileJson(const ModelImportProfile &profile);

} // namespace demi::assets
