#pragma once

#include <filesystem>

namespace demi::runtime {

struct RuntimeOptions {
  std::filesystem::path projectPath;
  int maxFrames = 0;
  bool serve = false;
};

[[nodiscard]] int runProject(const RuntimeOptions& options);

} // namespace demi::runtime
