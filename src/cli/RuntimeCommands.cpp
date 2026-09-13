#include "cli/RuntimeCommands.h"

#include "cli/CliArguments.h"
#include "cli/doctor/DoctorService.h"
#include "cli/project/ProjectDiscovery.h"
#include "demi/runtime/app/RuntimeApp.h"
#include "demi/runtime/platform/RuntimeCapabilities.h"

#include <filesystem>
#include <iostream>

namespace demi::cli {
namespace {

int numericValueAfter(const std::vector<std::string> &args,
                      const std::string &key) {
  const std::string value = valueAfter(args, key);
  if (value.empty())
    return 0;
  try {
    return std::stoi(value);
  } catch (...) {
    return 0;
  }
}

} // namespace

int runRuntimeCommand(const std::vector<std::string> &args,
                      const RuntimeCommandMode mode, std::ostream &output,
                      std::ostream &error) {
  const std::filesystem::path project = projectFileFromArgs(args);
  if (project.empty()) {
    error << (mode == RuntimeCommandMode::Develop ? "dev" : "run")
          << " could not find demi.project.json. Run inside a project or pass "
             "--project <project>.\n";
    return 2;
  }

  if (mode == RuntimeCommandMode::Develop) {
    std::vector<std::string> doctorArgs{"doctor", "--project",
                                        project.string()};
    if (doctor::runDoctorCommand(doctorArgs, output, error,
                                 runtime::hostRuntimeFeatures()) != 0)
      return 1;
    output << "Development mode: " << project.string()
           << " (watching source files)\n";
  }

  bool serve = mode == RuntimeCommandMode::Serve;
  std::pair<int, int> windowSize{0, 0};
  if (hasArg(args, "--window-size")) {
    const auto parsed = parseWindowSize(valueAfter(args, "--window-size"));
    if (!parsed) {
      error
          << "--window-size requires WIDTHxHEIGHT, each between 1 and 65535.\n";
      return 2;
    }
    windowSize = *parsed;
  }
  if (hasArg(args, "--profile-frames") &&
      (valueAfter(args, "--profile-frames").empty() ||
       valueAfter(args, "--profile-frames").starts_with("--"))) {
    error << "--profile-frames requires an output CSV path.\n";
    return 2;
  }
#ifdef DEMI_SERVER_CLI
  serve = true;
#endif
  return runtime::runProject(runtime::RuntimeOptions{
      .projectPath = project,
      .maxFrames = numericValueAfter(args, "--max-frames"),
      .serve = serve,
      .profiler = hasArg(args, "--profiler"),
      .watch = mode == RuntimeCommandMode::Develop || hasArg(args, "--watch"),
      .e2eTests = hasArg(args, "--e2e-tests"),
      .inputReplayPath = valueAfter(args, "--input-replay"),
      .profileReportPath = valueAfter(args, "--profile-report"),
      .debugOverlays = valueAfter(args, "--debug-overlays"),
      .windowWidth = windowSize.first,
      .windowHeight = windowSize.second,
      .profileFramesPath = valueAfter(args, "--profile-frames"),
  });
}

} // namespace demi::cli
