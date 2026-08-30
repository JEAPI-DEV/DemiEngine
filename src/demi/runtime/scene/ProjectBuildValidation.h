#pragma once

#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/ProjectBuildSettings.h"

#include <filesystem>

namespace demi {

struct AssetRegistry;

namespace runtime {

[[nodiscard]] Diagnostics validateProjectBuildAssets(
    const ProjectBuildSettings &settings, const AssetRegistry &registry,
    const std::filesystem::path &projectPath = {});

} // namespace runtime
} // namespace demi
