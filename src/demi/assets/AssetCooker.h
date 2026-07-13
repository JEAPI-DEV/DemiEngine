#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <string>

namespace demi::assets {

struct CookRequest {
  std::filesystem::path projectFile;
  std::filesystem::path outputDirectory;
  std::string platform = "linux";
};

[[nodiscard]] Diagnostics cookProject(const CookRequest &request);

} // namespace demi::assets
