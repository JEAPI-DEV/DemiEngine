#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace demi::assets {

struct SceneBudget3DReport {
  nlohmann::json document;
  Diagnostics diagnostics;
};

// Static, renderer-free scene cost inspection. Counts deliberately remain
// conservative estimates so this can run during validation and on CI/Noop.
[[nodiscard]] SceneBudget3DReport
inspectSceneBudget3D(const std::filesystem::path &projectFile,
                     const std::string &platform = "android");

} // namespace demi::assets
