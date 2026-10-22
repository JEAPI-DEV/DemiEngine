#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace demi::assets {

struct ModelLodReplacement {
  std::filesystem::path staged;
  std::filesystem::path destination;
};

// Installs already prepared files/directories as a group. On an ordinary I/O
// failure, restores previous destinations; failed recovery retains backups.
[[nodiscard]] bool
publishModelLods(const std::vector<ModelLodReplacement> &replacements,
                 const std::filesystem::path &workspace, std::string &error);

} // namespace demi::assets
