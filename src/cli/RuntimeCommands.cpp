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
#ifdef DEMI_SERVER_CLI
  serve = true;
#endif
  return runtime::runProject(runtime::RuntimeOptions{
      .projectPath = project,
      .maxFrames = numericValueAfter(args, "--max-frames"),
      .serve = serve,
      .profiler = hasArg(args, "--profiler"),
      .watch = mode == RuntimeCommandMode::Develop || hasArg(args, "--watch"),
      .mobileTests = hasArg(args, "--mobile-tests"),
      .inputReplayPath = valueAfter(args, "--input-replay"),
      .profileReportPath = valueAfter(args, "--profile-report"),
      .debugOverlays = valueAfter(args, "--debug-overlays"),
  });
}

} // namespace demi::cli
