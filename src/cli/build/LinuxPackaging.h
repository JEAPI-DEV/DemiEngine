#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>

namespace demi::build {

struct LinuxPackageRequest {
  std::filesystem::path projectFile;
  std::filesystem::path runtimeExecutable;
  std::filesystem::path stagingDirectory;
  bool server = false;
};

[[nodiscard]] Diagnostics stageLinuxPackage(const LinuxPackageRequest &request);

} // namespace demi::build
