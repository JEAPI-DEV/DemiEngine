#pragma once

#include <filesystem>
#include <string>

namespace demi::runtime {

struct RuntimeOptions {
  std::filesystem::path projectPath;
  int maxFrames = 0;
  bool serve = false;
  bool profiler = false;
  bool watch = false;
  bool e2eTests = false;
  std::filesystem::path inputReplayPath;
  std::filesystem::path profileReportPath;
  std::string debugOverlays;
  int windowWidth = 0;
  int windowHeight = 0;
  std::filesystem::path profileFramesPath;
};

[[nodiscard]] int runProject(const RuntimeOptions &options);

} // namespace demi::runtime
