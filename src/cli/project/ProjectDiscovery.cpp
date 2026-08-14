#include "cli/project/ProjectDiscovery.h"

#include "cli/CliArguments.h"

namespace demi::cli {

std::filesystem::path
projectFileFromArgs(const std::vector<std::string> &args,
                    std::filesystem::path startDirectory) {
  const std::string requested = valueAfter(args, "--project");
  if (requested.empty())
    return demi::findProjectFile(std::move(startDirectory));

  std::filesystem::path path = requested;
  std::error_code error;
  if (std::filesystem::is_directory(path, error))
    path /= "demi.project.json";
  return path;
}

} // namespace demi::cli
