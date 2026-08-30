#include "demi/filesystem/ProjectDiscovery.h"

namespace demi {

std::filesystem::path findProjectFile(std::filesystem::path startDirectory) {
  std::error_code error;
  startDirectory = std::filesystem::absolute(startDirectory, error);
  if (error)
    return {};
  if (!std::filesystem::is_directory(startDirectory, error))
    startDirectory = startDirectory.parent_path();

  while (!startDirectory.empty()) {
    const auto candidate = startDirectory / "demi.project.json";
    if (std::filesystem::is_regular_file(candidate, error))
      return candidate;
    error.clear();
    const auto parent = startDirectory.parent_path();
    if (parent == startDirectory)
      break;
    startDirectory = parent;
  }
  return {};
}

} // namespace demi
