#pragma once

#include <filesystem>

namespace demi {

[[nodiscard]] std::filesystem::path
findProjectFile(std::filesystem::path startDirectory);

} // namespace demi
