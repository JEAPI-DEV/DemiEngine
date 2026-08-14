#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace demi::cli {

[[nodiscard]] std::filesystem::path
findProjectFile(std::filesystem::path startDirectory);

[[nodiscard]] std::filesystem::path projectFileFromArgs(
    const std::vector<std::string> &args,
    std::filesystem::path startDirectory = std::filesystem::current_path());

} // namespace demi::cli
