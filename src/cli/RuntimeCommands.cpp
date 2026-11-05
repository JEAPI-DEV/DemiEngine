#include "cli/RuntimeCommands.h"

#include "cli/CliArguments.h"
#include "cli/CommandLine.h"
#include "cli/doctor/DoctorService.h"
#include "cli/project/ProjectDiscovery.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/runtime/app/RuntimeApp.h"
#include "demi/runtime/platform/RuntimeCapabilities.h"

#include <filesystem>
#include <iostream>

namespace demi::cli {
int runRuntimeCommand(const std::vector<std::string> &args,
                      const RuntimeCommandMode mode, std::ostream &output,
                      std::ostream &error) {
  CLI::App command{"Run a project", "demi " + args.front()};
  runtime::RuntimeOptions options;
  std::string requestedProject;
  std::string window;
  std::string target;
  command.add_option("target", target)->check(CLI::IsMember({"linux"}));
  command.add_option("--project", requestedProject);
  command.add_option("--max-frames", options.maxFrames)
      ->check(CLI::NonNegativeNumber);
  command.add_flag("--profiler", options.profiler);
  command.add_flag("--watch", options.watch);
  command.add_flag("--e2e-tests", options.e2eTests);
  command.add_option("--input-replay", options.inputReplayPath);
  command.add_option("--profile-report", options.profileReportPath);
  command.add_option("--profile-frames", options.profileFramesPath);
  command.add_option("--debug-overlays", options.debugOverlays);
  command.add_option("--window-size", window)
      ->check([](const std::string &value) {
        return parseWindowSize(value)
                   ? std::string{}
                   : "requires WIDTHxHEIGHT, each between 1 and 65535";
      });
  if (const auto status = parseCommand(command, args, 1, output, error)) {
    return *status;
  }
  const std::filesystem::path project = resolveProjectFile(requestedProject);
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

  options.projectPath = project;
  options.serve = mode == RuntimeCommandMode::Serve;
  options.watch = options.watch || mode == RuntimeCommandMode::Develop;
  if (!window.empty()) {
    const auto size = *parseWindowSize(window);
    options.windowWidth = size.first;
    options.windowHeight = size.second;
  }
#ifdef DEMI_SERVER_CLI
  options.serve = true;
#endif
  try {
    // Development tools prepare source assets before entering the runtime.
    // Shipped binary assets pass through without generation.
    assets::prepareTerrainAssets(loadAssetRegistry(project.parent_path()));
  } catch (const std::exception &exception) {
    error << "Terrain asset preparation failed: " << exception.what() << '\n';
    return 1;
  }
  return runtime::runProject(options);
}

} // namespace demi::cli
