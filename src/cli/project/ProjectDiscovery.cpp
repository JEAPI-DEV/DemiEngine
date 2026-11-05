#include "cli/project/ProjectDiscovery.h"

namespace demi::cli {

std::filesystem::path resolveProjectFile(const std::string &requested,
                                         std::filesystem::path startDirectory) {
  if (requested.empty())
    return demi::findProjectFile(std::move(startDirectory));

  std::filesystem::path path = requested;
  std::error_code error;
  if (std::filesystem::is_directory(path, error))
    path /= "demi.project.json";
  return path;
}

} // namespace demi::cli
