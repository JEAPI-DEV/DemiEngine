#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace demi::runtime {

struct PackageTestResult {
  int passed = 0;
  int failed = 0;
  std::vector<std::string> failures;
  Diagnostics diagnostics;
};

[[nodiscard]] PackageTestResult
runPackageTests(const std::filesystem::path &packageRoot,
                const std::vector<std::filesystem::path> &testFiles,
                std::uint32_t seed = 1);

} // namespace demi::runtime
