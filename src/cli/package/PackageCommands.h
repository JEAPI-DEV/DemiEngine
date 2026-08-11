#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace demi::cli::package_commands {

struct PackageCommandContext {
  std::filesystem::path engineRoot;
};

int runPackageCommand(const std::vector<std::string> &args,
                      const PackageCommandContext &context,
                      std::ostream &output, std::ostream &error);

} // namespace demi::cli::package_commands
