#pragma once

#include <filesystem>
#include <string>

namespace demi::runtime {

struct RuntimeOptions {
  std::filesystem::path projectPath;
  int maxFrames = 0;
  bool serve = false;
  std::filesystem::path inputReplayPath;
  std::filesystem::path profileReportPath;
  std::string debugOverlays;
};

[[nodiscard]] int runProject(const RuntimeOptions &options);

} // namespace demi::runtime
