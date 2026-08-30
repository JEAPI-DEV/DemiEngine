#include "cli/BuildCommands.h"

#include "cli/CliArguments.h"
#include "cli/build/BuildService.h"
#include "cli/project/ProjectDiscovery.h"
#include "demi/runtime/scene/ProjectBuildSettings.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

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

int inspectBuildSettings(const std::filesystem::path &projectPath) {
  std::ifstream input(projectPath);
  if (!input) {
    std::cerr << "Could not read project: " << projectPath.string() << '\n';
    return 1;
  }

  nlohmann::json project;
  try {
    input >> project;
  } catch (const nlohmann::json::exception &error) {
    std::cerr << "Could not parse project JSON: " << error.what() << '\n';
    return 1;
  }

  const runtime::ProjectBuildSettingsResult result =
      runtime::parseProjectBuildSettings(project, projectPath);
  if (!result.diagnostics.empty())
    printDiagnosticsText(std::cerr, result.diagnostics);
  if (hasErrors(result.diagnostics))
    return 1;

  std::cout << runtime::projectBuildSettingsJson(result.settings).dump(2)
            << '\n';
  return 0;
}

} // namespace

int runBuildCommand(const std::vector<std::string> &args,
                    const BuildContext &context) {
  if (args.size() < 2) {
    std::cerr
        << "build requires a target: inspect, apk, linux, or linux_server.\n";
    return 2;
  }
  const std::filesystem::path project = projectFileFromArgs(args);
  if (project.empty()) {
    std::cerr << "build requires --project <project> or a demi.project.json in "
                 "the current directory.\n";
    return 2;
  }

  if (args[1] == "inspect")
    return inspectBuildSettings(std::filesystem::absolute(project));

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
