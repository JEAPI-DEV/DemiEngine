#include "cli/BuildCommands.h"

#include "cli/CommandLine.h"
#include "cli/build/BuildService.h"
#include "cli/project/ProjectDiscovery.h"
#include "demi/runtime/platform/RuntimeCapabilities.h"
#include "demi/runtime/scene/ProjectBuildSettings.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace demi::cli {
namespace {

std::filesystem::path defaultOutput(const std::filesystem::path &project,
                                    const std::string &target) {
  if (target == "apk" || target == "aab")
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
  CLI::App command{"Inspect or package a project", "demi build"};
  std::string target;
  std::string requestedProject;
  std::string configuration;
  std::string requestedOutput;
  std::string gradle = "gradle";
  command.add_option("target", target)
      ->required()
      ->check(
          CLI::IsMember({"inspect", "apk", "aab", "linux", "linux_server"}));
  command.add_option("--project", requestedProject);
  command.add_option("--configuration", configuration)
      ->check(CLI::IsMember({"debug", "release"}));
  command.add_option("--output", requestedOutput);
  command.add_option("--gradle", gradle);
  if (const auto status =
          parseCommand(command, args, 1, std::cout, std::cerr)) {
    return *status;
  }
  const auto project = resolveProjectFile(requestedProject);
  if (project.empty()) {
    std::cerr << "build requires --project <project> or a demi.project.json in "
                 "the current directory.\n";
    return 2;
  }

  if (target == "inspect")
    return inspectBuildSettings(std::filesystem::absolute(project));

  if (target == "aab" && configuration == "debug") {
    std::cerr << "AAB output is a release workflow; omit --configuration or "
                 "use release.\n";
    return 2;
  }

  build::ProjectOperation operation;
  if (target == "linux") {
    operation = build::ProjectOperation::PackageLinux;
  } else if (target == "linux_server") {
    operation = build::ProjectOperation::PackageLinuxServer;
  } else if (target == "apk") {
    operation = configuration == "release"
                    ? build::ProjectOperation::PackageAndroidRelease
                    : build::ProjectOperation::PackageAndroid;
  } else {
    operation = build::ProjectOperation::BundleAndroidRelease;
  }

  const std::filesystem::path absoluteProject =
      std::filesystem::absolute(project);
  const build::ProjectOperationResult result = build::runProjectOperation(
      {.operation = operation,
       .projectFile = absoluteProject,
       .outputDirectory = requestedOutput.empty()
                              ? defaultOutput(absoluteProject, target)
                              : std::filesystem::path(requestedOutput),
       .engineRoot = context.engineRoot,
       .runtimeExecutable = std::filesystem::absolute(context.executablePath),
       .gradleExecutable = gradle,
       .hostFeatures = runtime::hostRuntimeFeatures()});
  if (!result.diagnostics.empty())
    printDiagnosticsText(std::cerr, result.diagnostics);
  if (!result.succeeded())
    return 1;
  std::cout << "Wrote "
            << (target == "apk"            ? "Android APK: "
                : target == "aab"          ? "Android App Bundle: "
                : target == "linux_server" ? "Linux server bundle: "
                                           : "Linux bundle: ")
            << result.artifact.string() << '\n';
  return 0;
}

} // namespace demi::cli
