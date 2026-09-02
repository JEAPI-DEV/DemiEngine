#pragma once

#include "demi/capabilities/PlatformCapabilities.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/ProjectBuildSettings.h"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace demi {

struct AssetRegistry;

namespace runtime {

[[nodiscard]] Diagnostics validateProjectBuildAssets(
    const ProjectBuildSettings &settings, const AssetRegistry &registry,
    const std::filesystem::path &projectPath = {});

// Optional runtime features the authored project data reaches. Asset types
// are sourced from the project's asset registry because every registry asset
// is cooked into release packages. Component and Lua-service evidence is
// sourced from authored scene, prefab, HUD, and Lua files.
struct ProjectFeatureUsage {
  bool network = false;
  bool media = false;
  bool svg = false;
  std::vector<std::string> networkEvidence;
  std::vector<std::string> mediaEvidence;
  std::vector<std::string> svgEvidence;
};

[[nodiscard]] ProjectFeatureUsage scanProjectFeatureUsage(
    const nlohmann::json &project,
    const std::filesystem::path &projectDirectory,
    const AssetRegistry &registry);

// Cross-checks reachable project features and declared Android permissions
// against what the target platform's packaged runtime supports.
[[nodiscard]] Diagnostics validateProjectPlatformCapabilities(
    const ProjectBuildSettings &settings, const ProjectFeatureUsage &usage,
    capabilities::TargetPlatform platform,
    const capabilities::RuntimeFeatures &features,
    const std::filesystem::path &projectPath);

// Convenience entry point for CLI, doctor, and packaging callers: loads the
// authored project, scans reachable features, and validates them for the
// requested target platform. Reports only capability diagnostics; source
// validation is the caller's separate `demi validate` pass.
[[nodiscard]] Diagnostics validateProjectPlatformCapabilities(
    const std::filesystem::path &projectPath,
    capabilities::TargetPlatform platform,
    const capabilities::RuntimeFeatures &hostRendererRuntime);

} // namespace runtime
} // namespace demi
