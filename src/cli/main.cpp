#include "cli/AssetCommands.h"
#include "cli/BuildCommands.h"
#include "cli/CapabilityCommands.h"
#include "cli/CommandLine.h"
#include "cli/CookCommands.h"
#include "cli/HudCommands.h"
#include "cli/LuaStubExport.h"
#include "cli/RuntimeCommands.h"
#include "cli/SceneCompositionCommands.h"
#include "cli/TerrainCommands.h"
#include "cli/TestCommands.h"
#include "cli/doctor/DoctorService.h"
#include "cli/package/PackageCommands.h"
#include "cli/project/ProjectDiscovery.h"
#include "cli/project/ProjectTemplates.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/capabilities/PlatformCapabilities.h"
#include "demi/core/Version.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/filesystem/AtomicTextFile.h"
#include "demi/runtime/app/RuntimeApp.h"
#include "demi/runtime/platform/RuntimeCapabilities.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/ProjectBuildValidation.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/schema/Validation.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {

using demi::cli::parseCommand;

constexpr int ExitSuccess = 0;
constexpr int ExitValidationFailure = 1;
constexpr int ExitUsageError = 2;

std::filesystem::path sourceRoot() {
#ifdef DEMI_SOURCE_DIR
  return std::filesystem::path(DEMI_SOURCE_DIR);
#else
  return std::filesystem::current_path();
#endif
}

// argv[0] may be a bare command name when the binary is found through PATH.
// Child tools need a real path, so resolve slash-relative names against the
// working directory and bare names through PATH before handing them on.
std::filesystem::path
resolveSelfExecutable(const std::filesystem::path &argv0) {
  std::error_code error;
  if (!argv0.empty()) {
    if (argv0.is_absolute()) {
      if (std::filesystem::is_regular_file(argv0, error))
        return argv0;
    } else if (argv0.string().find('/') != std::string::npos) {
      const std::filesystem::path absolute =
          std::filesystem::absolute(argv0, error);
      if (std::filesystem::is_regular_file(absolute, error))
        return absolute;
    } else if (const char *pathVariable = std::getenv("PATH")) {
      std::istringstream directories(pathVariable);
      std::string directory;
      while (std::getline(directories, directory, ':')) {
        if (directory.empty())
          continue;
        const std::filesystem::path candidate =
            std::filesystem::path(directory) / argv0;
        if (std::filesystem::is_regular_file(candidate, error))
          return candidate;
      }
    }
  }
  return argv0;
}

int launchAndroidDeviceTool(const std::string &mode,
                            const std::vector<std::string> &args,
                            const std::size_t forwardedFrom,
                            const std::filesystem::path &demiExecutable) {
#if defined(__linux__)
  CLI::App parser{"Run or qualify a project on an attached Android device",
                  "demi " + args.front() + " android"};
  std::string requestedProject;
  std::string adb;
  std::string serial;
  std::string apk;
  std::string output;
  bool watch = false;
  double settleSeconds = 2.0;
  parser.add_option("--project", requestedProject);
  parser.add_option("--adb", adb);
  parser.add_option("--serial", serial);
  parser.add_option("--apk", apk);
  parser.add_option("--output", output);
  parser.add_flag("--watch", watch);
  auto *settle = parser.add_option("--settle-seconds", settleSeconds)
                     ->check(CLI::NonNegativeNumber);
  if (const auto status =
          parseCommand(parser, args, forwardedFrom, std::cout, std::cerr)) {
    return *status;
  }
  const auto project = demi::cli::resolveProjectFile(requestedProject);
  if (project.empty()) {
    std::cerr << "No demi.project.json was found. Pass --project <project>.\n";
    return ExitUsageError;
  }
  std::vector<std::string> command{
      "python3",
      (sourceRoot() / "scripts/android_device.py").string(),
      mode,
      "--demi-executable",
      resolveSelfExecutable(demiExecutable).string(),
      "--project",
      std::filesystem::absolute(project).string()};
  for (const auto &[option, value] : {std::pair{"--adb", adb},
                                      {"--serial", serial},
                                      {"--apk", apk},
                                      {"--output", output}}) {
    if (!value.empty()) {
      command.insert(command.end(), {option, value});
    }
  }
  if (watch) {
    command.push_back("--watch");
  }
  if (*settle) {
    command.insert(command.end(),
                   {"--settle-seconds", std::to_string(settleSeconds)});
  }
  std::vector<char *> native;
  native.reserve(command.size() + 1);
  for (std::string &argument : command)
    native.push_back(argument.data());
  native.push_back(nullptr);
  execvp(native.front(), native.data());
  std::cerr << "Could not launch the Android device tool.\n";
  return ExitValidationFailure;
#else
  (void)mode;
  (void)args;
  (void)forwardedFrom;
  (void)demiExecutable;
  std::cerr << "Android device workflows currently require a Linux host.\n";
  return ExitUsageError;
#endif
}

int runValidate(const std::vector<std::string> &args) {
  CLI::App command{"Validate authored source and target capabilities",
                   "demi validate"};
  std::filesystem::path target = ".";
  std::string format = "text";
  std::string platform;
  command.add_option("path", target);
  command.add_option("--format", format)
      ->check(CLI::IsMember({"text", "json"}));
  command.add_option("--platform", platform)
      ->check(CLI::IsMember({"linux", "linux_server", "android"}));
  if (const auto status =
          parseCommand(command, args, 1, std::cout, std::cerr)) {
    return *status;
  }

  const demi::ValidationSummary summary =
      demi::classifySourceFile(target) == demi::SourceFileKind::Project
          ? demi::validateProjectPath(target)
          : demi::validatePath(target);
  demi::Diagnostics diagnostics = summary.diagnostics;
  if (!platform.empty()) {
    std::optional<demi::capabilities::TargetPlatform> platformTarget;
    if (platform == "linux")
      platformTarget = demi::capabilities::TargetPlatform::Linux;
    else if (platform == "linux_server")
      platformTarget = demi::capabilities::TargetPlatform::LinuxServer;
    else if (platform == "android")
      platformTarget = demi::capabilities::TargetPlatform::Android;
    if (!platformTarget) {
      std::cerr << "validate --platform must be linux, linux_server, or "
                   "android.\n";
      return ExitUsageError;
    }
    const std::filesystem::path project = std::filesystem::is_directory(target)
                                              ? target / "demi.project.json"
                                              : target;
    if (std::filesystem::is_regular_file(project)) {
      const demi::Diagnostics capabilities =
          demi::runtime::validateProjectPlatformCapabilities(
              project, *platformTarget, demi::runtime::hostRuntimeFeatures());
      diagnostics.insert(diagnostics.end(), capabilities.begin(),
                         capabilities.end());
    }
  }

  if (format == "json") {
    demi::printDiagnosticsJson(std::cout, diagnostics);
  } else {
    std::cout << "Checked " << summary.checkedFiles << " file(s).\n";
    demi::printDiagnosticsText(std::cout, diagnostics);
  }

  return demi::hasErrors(diagnostics) ? ExitValidationFailure : ExitSuccess;
}

int launchEditor(const std::vector<std::string> &args) {
#if defined(__linux__) && !defined(DEMI_SERVER_CLI)
  CLI::App command{"Open the native editor", "demi editor"};
  std::string requestedProject;
  int maxFrames = 0;
  int maxSeconds = 0;
  std::string profileFrames;
  command.add_option("--project", requestedProject);
  auto *frames = command.add_option("--max-frames", maxFrames)
                     ->check(CLI::NonNegativeNumber);
  auto *seconds = command.add_option("--max-seconds", maxSeconds)
                      ->check(CLI::NonNegativeNumber);
  command.add_option("--profile-frames", profileFrames);
  if (const auto status =
          parseCommand(command, args, 1, std::cout, std::cerr)) {
    return *status;
  }
  const auto project = demi::cli::resolveProjectFile(requestedProject);
  if (project.empty()) {
    std::cerr << "No demi.project.json was found. Pass --project <project>.\n";
    return ExitUsageError;
  }
  std::error_code error;
  const std::filesystem::path executable =
      std::filesystem::read_symlink("/proc/self/exe", error);
  if (error) {
    std::cerr << "Could not locate the DemiEngine executable: "
              << error.message() << '\n';
    return ExitValidationFailure;
  }
  const std::filesystem::path editor = executable.parent_path() / "demi-editor";
  std::vector<std::string> arguments{editor.string(), "--project",
                                     project.string()};
  if (*frames) {
    arguments.insert(arguments.end(),
                     {"--max-frames", std::to_string(maxFrames)});
  }
  if (*seconds) {
    arguments.insert(arguments.end(),
                     {"--max-seconds", std::to_string(maxSeconds)});
  }
  if (!profileFrames.empty()) {
    arguments.insert(arguments.end(), {"--profile-frames", profileFrames});
  }
  std::vector<char *> nativeArguments;
  nativeArguments.reserve(arguments.size() + 1);
  for (std::string &argument : arguments)
    nativeArguments.push_back(argument.data());
  nativeArguments.push_back(nullptr);
  execv(editor.c_str(), nativeArguments.data());
  std::cerr << "Could not launch " << editor << ".\n";
  return ExitValidationFailure;
#else
  (void)args;
  std::cerr << "The graphical editor is available in desktop Linux builds.\n";
  return ExitUsageError;
#endif
}

int runScene(const std::vector<std::string> &args) {
  if (args.size() >= 2 && args[1] == "diff") {
    return demi::cli::runSceneDiff(args, std::cout, std::cerr);
  }
  if (args.size() >= 2 && args[1] == "expand") {
    return demi::cli::runSceneExpand(args, std::cout, std::cerr);
  }
  CLI::App command{"Inspect authored scenes", "demi scene"};
  command.require_subcommand(1, 1);
  std::string path;
  for (const auto *name : {"list", "inspect"}) {
    command.add_subcommand(name)->add_option("path", path)->required();
  }
  command.add_subcommand("diff", "Compare two expanded scenes");
  command.add_subcommand("expand", "Expand scene composition");
  if (const auto status =
          parseCommand(command, args, 1, std::cout, std::cerr)) {
    return *status;
  }
  const auto subcommand = command.get_subcommands().front()->get_name();
  if (subcommand == "list") {
    const std::filesystem::path project = path;
    const auto references = demi::extractSceneReferences(project);
    for (const std::string &reference : references) {
      std::cout << reference << '\n';
    }
    return references.empty() ? ExitValidationFailure : ExitSuccess;
  }

  const std::filesystem::path scene = path;
  const demi::ValidationSummary summary = demi::validatePath(scene);
  demi::printDiagnosticsText(std::cout, summary.diagnostics);
  if (!demi::hasErrors(summary.diagnostics)) {
    std::cout << "Scene inspection complete: " << scene.string() << '\n';
  }
  return demi::hasErrors(summary.diagnostics) ? ExitValidationFailure
                                              : ExitSuccess;
}

int runSave(const std::vector<std::string> &args) {
  CLI::App command{"Inspect a save document", "demi save"};
  command.require_subcommand(1, 1);
  std::filesystem::path save;
  command.add_subcommand("inspect")->add_option("path", save)->required();
  if (const auto status =
          parseCommand(command, args, 1, std::cout, std::cerr)) {
    return *status;
  }
  const demi::ValidationSummary summary = demi::validatePath(save);
  demi::printDiagnosticsText(std::cout, summary.diagnostics);
  if (!demi::hasErrors(summary.diagnostics)) {
    std::cout << "Save inspection complete: " << save.string() << '\n';
  }
  return demi::hasErrors(summary.diagnostics) ? ExitValidationFailure
                                              : ExitSuccess;
}

int runScript(const std::vector<std::string> &args) {
  CLI::App command{"Check Lua syntax", "demi script"};
  command.require_subcommand(1, 1);
  std::string path;
  command.add_subcommand("check")->add_option("path", path)->required();
  if (const auto status =
          parseCommand(command, args, 1, std::cout, std::cerr)) {
    return *status;
  }
  const demi::Diagnostics diagnostics =
      demi::runtime::LuaScriptHost::checkScriptSyntax(path);
  demi::printDiagnosticsText(std::cout, diagnostics);
  return demi::hasErrors(diagnostics) ? ExitValidationFailure : ExitSuccess;
}

int runLuaStubs(const std::vector<std::string> &args) {
  CLI::App command{"Export Lua language-server metadata", "demi lua-stubs"};
  command.require_subcommand(1, 1);
  std::filesystem::path output = ".demi/lua";
  command.add_subcommand("generate")->add_option("directory", output);
  if (const auto status =
          parseCommand(command, args, 1, std::cout, std::cerr)) {
    return *status;
  }
  std::string error;
  if (!demi::cli::exportLuaStubs(sourceRoot() / "scripts/stubs", output,
                                 error)) {
    std::cerr << error << '\n';
    return ExitValidationFailure;
  }
  std::cout << "Wrote LuaLS module library: " << output.string() << '\n';
  return ExitSuccess;
}

int runSchema(const std::vector<std::string> &args) {
  CLI::App command{"Export component schemas", "demi schema"};
  command.require_subcommand(1, 1);
  std::filesystem::path outputPath =
      sourceRoot() / "schemas" / "components.schema.json";
  command.add_subcommand("export")->add_option("path", outputPath);
  if (const auto status =
          parseCommand(command, args, 1, std::cout, std::cerr)) {
    return *status;
  }
  try {
    const std::string content =
        demi::runtime::scene_loading::canonicalComponentSchema().dump(2) + '\n';
    std::error_code error;
    if (!demi::atomicWriteText(outputPath, content, error)) {
      std::cerr << "Failed to write component schema: " << outputPath << ": "
                << error.message() << '\n';
      return ExitValidationFailure;
    }
  } catch (const std::exception &error) {
    std::cerr << "Failed to export component schema: " << outputPath << ": "
              << error.what() << '\n';
    return ExitValidationFailure;
  } catch (...) {
    std::cerr << "Failed to export component schema: " << outputPath
              << ": unknown exception\n";
    return ExitValidationFailure;
  }
  std::cout << "Wrote component schema: " << outputPath << '\n';
  return ExitSuccess;
}

int runSmokeTests(const std::vector<std::string> &args) {
  CLI::App command{"Validate and smoke-test a project", "demi test"};
  std::string requestedProject;
  command.add_option("--project", requestedProject);
  if (const auto status =
          parseCommand(command, args, 1, std::cout, std::cerr)) {
    return *status;
  }
  const auto project = demi::cli::resolveProjectFile(requestedProject);
  if (!project.empty()) {
    const demi::ValidationSummary validation =
        demi::validateProjectPath(project);
    demi::printDiagnosticsText(std::cout, validation.diagnostics);
    if (demi::hasErrors(validation.diagnostics))
      return ExitValidationFailure;
    return demi::runtime::runProject({.projectPath = project, .maxFrames = 1});
  }
  const demi::ValidationSummary summary = demi::validatePath("examples");
  demi::printDiagnosticsText(std::cout, summary.diagnostics);
  return demi::hasErrors(summary.diagnostics) ? ExitValidationFailure
                                              : ExitSuccess;
}

} // namespace

int main(int argc, char **argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  CLI::App command{"DemiEngine CLI", "demi"};
  command.require_subcommand(0, 1);
  int result = ExitSuccess;
  // Families own their option declarations. Prefix dispatch leaves their argv
  // intact so there is one parser and one generated help source per family.
  const auto add = [&](const char *name, const char *description, auto run) {
    auto *family = command.add_subcommand(name, description);
    family->set_help_flag("");
    family->prefix_command();
    family->callback([&, run] { result = run(); });
  };
  command.add_subcommand("version", "Print engine version")->callback([] {
    std::cout << demi::EngineName << ' ' << demi::EngineVersion << '\n';
  });
  add("new", "Create a project from a template", [&] {
    return demi::cli::project::runNewCommand(args, sourceRoot() / "templates",
                                             std::cout, std::cerr);
  });
  add("doctor", "Inspect tooling and platform capabilities", [&] {
    return demi::cli::doctor::runDoctorCommand(
        args, std::cout, std::cerr, demi::runtime::hostRuntimeFeatures());
  });
  add("validate", "Validate authored source",
      [&] { return runValidate(args); });
  add("schema", "Export component schemas", [&] { return runSchema(args); });
  add("hud", "Inspect resolved HUD layout",
      [&] { return demi::cli::runHudCommand(args, std::cout, std::cerr); });
  add("capabilities", "Export and compare the public contract", [&] {
    return demi::cli::runCapabilityCommand(args, {.sourceRoot = sourceRoot()},
                                           std::cout, std::cerr);
  });
  add("scene", "Inspect and expand scenes", [&] { return runScene(args); });
  add("prefab", "Inspect a prefab",
      [&] { return demi::cli::runPrefabCommand(args, std::cout, std::cerr); });
  add("terrain", "Author and inspect terrain",
      [&] { return demi::cli::runTerrainCommand(args, std::cout, std::cerr); });
  add("asset", "Import, inspect and package assets",
      [&] { return demi::cli::runAssetCommand(args, std::cout, std::cerr); });
  add("package", "Manage project packages", [&] {
    return demi::cli::package_commands::runPackageCommand(
        args, {.engineRoot = sourceRoot()}, std::cout, std::cerr);
  });
  add("cook", "Cook project assets",
      [&] { return demi::cli::runCookCommand(args, std::cout, std::cerr); });
  add("save", "Inspect saves", [&] { return runSave(args); });
  add("script", "Check Lua syntax", [&] { return runScript(args); });
  add("lua-stubs", "Export Lua language-server metadata",
      [&] { return runLuaStubs(args); });
  add("test", "Run smoke or desktop/Android end-to-end tests", [&] {
    if (args.size() >= 2 && args[1] == "android") {
      return launchAndroidDeviceTool("qualify", args, 2, argv[0]);
    }
    if (args.size() >= 2 && args[1] == "linux") {
      return demi::cli::runTestLinuxCommand(args, std::cout, std::cerr,
                                            resolveSelfExecutable(argv[0]));
    }
    return runSmokeTests(args);
  });
  add("build", "Package a Linux or Android target", [&] {
    return demi::cli::runBuildCommand(
        args, {.engineRoot = sourceRoot(), .executablePath = argv[0]});
  });
  for (const auto *name : {"run", "dev", "serve"}) {
    add(name, "Run a project (run android delegates to the device tool)",
        [&, name] {
          if (args.size() >= 2 && args[1] == "android" &&
              std::string_view(name) == "run") {
            return launchAndroidDeviceTool("run", args, 2, argv[0]);
          }
          const auto mode = std::string_view(name) == "dev"
                                ? demi::cli::RuntimeCommandMode::Develop
                            : std::string_view(name) == "serve"
                                ? demi::cli::RuntimeCommandMode::Serve
                                : demi::cli::RuntimeCommandMode::Run;
          return demi::cli::runRuntimeCommand(args, mode, std::cout, std::cerr);
        });
  }
  add("editor", "Open the native editor", [&] { return launchEditor(args); });
  if (args.empty() || args.front() == "help") {
    std::cout << command.help();
    return ExitSuccess;
  }
  if (const auto status =
          parseCommand(command, args, 0, std::cout, std::cerr)) {
    return *status;
  }
  return result;
}
