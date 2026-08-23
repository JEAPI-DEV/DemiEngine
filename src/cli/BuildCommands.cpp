#include "cli/BuildCommands.h"

#include "cli/CliArguments.h"
#include "cli/build/BuildService.h"
#include "cli/project/ProjectDiscovery.h"

#include <filesystem>
#include <iostream>

namespace demi::cli {
namespace {

std::filesystem::path defaultOutput(const std::filesystem::path &project,
                                    const std::string &target) {
  if (target == "apk")
    return {};
  std::string name = project.parent_path().filename().string();
  if (name.empty())
    name = "demi-game";
  return std::filesystem::current_path() / "build" /
         (target == "linux_server" ? "linux_server" : "linux") / name;
}

} // namespace

int runBuildCommand(const std::vector<std::string> &args,
                    const BuildContext &context) {
  if (args.size() < 2) {
    std::cerr << "build requires a target: apk, linux, or linux_server.\n";
    return 2;
  }
  const std::filesystem::path project = projectFileFromArgs(args);
  if (project.empty()) {
    std::cerr << "build requires --project <project> or a demi.project.json in "
                 "the current directory.\n";
    return 2;
  }

  build::ProjectOperation operation;
  if (args[1] == "linux")
    operation = build::ProjectOperation::PackageLinux;
  else if (args[1] == "linux_server")
    operation = build::ProjectOperation::PackageLinuxServer;
  else if (args[1] == "apk")
    operation = build::ProjectOperation::PackageAndroid;
  else {
    std::cerr << "Unknown build target: " << args[1] << '\n';
    return 2;
  }

  const std::filesystem::path absoluteProject =
      std::filesystem::absolute(project);
  const std::string requestedOutput = valueAfter(args, "--output");
  const build::ProjectOperationResult result = build::runProjectOperation(
      {.operation = operation,
       .projectFile = absoluteProject,
       .outputDirectory = requestedOutput.empty()
                              ? defaultOutput(absoluteProject, args[1])
                              : std::filesystem::path(requestedOutput),
       .engineRoot = context.engineRoot,
       .runtimeExecutable = std::filesystem::absolute(context.executablePath),
       .gradleExecutable = valueAfter(args, "--gradle").empty()
                               ? "gradle"
                               : valueAfter(args, "--gradle")});
  if (!result.diagnostics.empty())
    printDiagnosticsText(std::cerr, result.diagnostics);
  if (!result.succeeded())
    return 1;
  std::cout << "Wrote "
            << (args[1] == "apk"            ? "Android package: "
                : args[1] == "linux_server" ? "Linux server bundle: "
                                            : "Linux bundle: ")
            << result.artifact.string() << '\n';
  return 0;
}

} // namespace demi::cli
