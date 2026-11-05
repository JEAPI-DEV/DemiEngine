#pragma once

#include "demi/filesystem/ProjectDiscovery.h"

#include <filesystem>
#include <string>
#include <vector>

namespace demi::cli {

using ::demi::findProjectFile;

[[nodiscard]] std::filesystem::path resolveProjectFile(
    const std::string &requested,
    std::filesystem::path startDirectory = std::filesystem::current_path());

} // namespace demi::cli
