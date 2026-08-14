#pragma once

#include "demi/filesystem/ProjectDiscovery.h"

#include <filesystem>
#include <string>
#include <vector>

namespace demi::cli {

using ::demi::findProjectFile;

[[nodiscard]] std::filesystem::path projectFileFromArgs(
    const std::vector<std::string> &args,
    std::filesystem::path startDirectory = std::filesystem::current_path());

} // namespace demi::cli
