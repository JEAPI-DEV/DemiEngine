#include "cli/project/ProjectDiscovery.h"

#include "cli/CliArguments.h"

namespace demi::cli {

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

std::filesystem::path
projectFileFromArgs(const std::vector<std::string> &args,
                    std::filesystem::path startDirectory) {
  const std::string requested = valueAfter(args, "--project");
  if (requested.empty())
    return findProjectFile(std::move(startDirectory));

  std::filesystem::path path = requested;
  std::error_code error;
  if (std::filesystem::is_directory(path, error))
    path /= "demi.project.json";
  return path;
}

} // namespace demi::cli
