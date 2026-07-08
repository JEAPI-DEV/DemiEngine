#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace demi::cli {

struct BuildContext {
  std::filesystem::path engineRoot;
  std::string executablePath;
};

[[nodiscard]] std::filesystem::path projectFileFromArgs(const std::vector<std::string>& args);
[[nodiscard]] int runBuildCommand(const std::vector<std::string>& args, const BuildContext& context);

} // namespace demi::cli
