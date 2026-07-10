#include "cli/BuildCommands.h"

#include "demi/schema/Validation.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace demi::cli {

namespace {

constexpr int ExitSuccess = 0;
constexpr int ExitValidationFailure = 1;
constexpr int ExitUsageError = 2;

enum class LinuxBundleMode {
  Game,
  Server,
};

std::string valueAfter(const std::vector<std::string>& args, const std::string& key) {
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == key) {
      return args[i + 1];
    }
  }
  return {};
}

std::filesystem::path defaultProjectFile() {
  const std::filesystem::path candidate = std::filesystem::current_path() / "demi.project.json";
  return std::filesystem::exists(candidate) ? candidate : std::filesystem::path{};
}

std::string shellQuote(const std::string& value) {
  std::string quoted = "'";
  for (const char character : value) {
    if (character == '\'') {
      quoted += "'\\''";
    } else {
      quoted += character;
    }
  }
  quoted += "'";
  return quoted;
}

std::string shellQuote(const std::filesystem::path& path) {
  return shellQuote(path.string());
}

std::string launcherNameFor(const std::filesystem::path& projectFile) {
  const std::string name = projectFile.parent_path().filename().string();
  return name.empty() ? "demi-game" : name;
}

std::string runtimeCommand() {
  return "\"$DIR/bin/demi\"";
}

bool shouldSkipBundlePath(const std::filesystem::path& relativePath, const LinuxBundleMode mode) {
  if (relativePath.empty()) {
    return false;
  }
  const std::string first = relativePath.begin()->string();
  if (first == ".git" || first == "build" || first == "generated") {
    return true;
  }
  return mode == LinuxBundleMode::Server && first == "tests";
}

bool copyProjectDirectory(const std::filesystem::path& source,
                          const std::filesystem::path& target,
                          const LinuxBundleMode mode,
                          std::string& error) {
  std::error_code code;
  std::filesystem::remove_all(target, code);
  code.clear();
  std::filesystem::create_directories(target, code);
  if (code) {
    error = "Failed to create output project directory: " + target.string();
    return false;
  }

  for (std::filesystem::recursive_directory_iterator entry(source, code), end; entry != end; entry.increment(code)) {
    if (code) {
      error = "Failed to scan project directory: " + code.message();
      return false;
    }

    const std::filesystem::path relativePath = std::filesystem::relative(entry->path(), source, code);
    if (code) {
      error = "Failed to resolve project path: " + code.message();
      return false;
    }
    if (shouldSkipBundlePath(relativePath, mode)) {
      if (entry->is_directory()) {
        entry.disable_recursion_pending();
      }
      continue;
    }

    const std::filesystem::path outputPath = target / relativePath;
    if (entry->is_directory()) {
      std::filesystem::create_directories(outputPath, code);
    } else if (entry->is_regular_file()) {
      std::filesystem::create_directories(outputPath.parent_path(), code);
      if (!code) {
        std::filesystem::copy_file(entry->path(), outputPath, std::filesystem::copy_options::overwrite_existing, code);
      }
    }
    if (code) {
      error = "Failed to copy project entry: " + relativePath.string();
      return false;
    }
  }
  return true;
}

bool copyRuntimeExecutable(const std::string& executablePath, const std::filesystem::path& outputRoot, std::string& error) {
  const std::filesystem::path source = std::filesystem::absolute(executablePath);
  const std::filesystem::path target = outputRoot / "bin" / "demi";
  std::error_code code;
  std::filesystem::create_directories(target.parent_path(), code);
  if (!code) {
    std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, code);
  }
  if (code) {
    error = "Failed to copy Demi runtime: " + source.string();
    return false;
  }
  std::filesystem::permissions(target,
                               std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec,
                               std::filesystem::perm_options::add,
                               code);
  return true;
}

std::filesystem::path defaultLinuxOutputRoot(const std::filesystem::path& projectFile, const LinuxBundleMode mode) {
  const std::string folder = mode == LinuxBundleMode::Server ? "linux_server" : "linux";
  return std::filesystem::current_path() / "build" / folder / launcherNameFor(projectFile);
}

std::string launcherFileNameFor(const std::filesystem::path& projectFile, const LinuxBundleMode mode) {
  return mode == LinuxBundleMode::Server ? "serve" : launcherNameFor(projectFile);
}

bool writeLinuxLauncher(const std::filesystem::path& launcherPath,
                        const std::string& runtimeCommand,
                        const LinuxBundleMode mode,
                        std::string& error) {
  std::error_code code;
  std::filesystem::create_directories(launcherPath.parent_path(), code);
  std::ofstream launcher(launcherPath);
  if (!launcher) {
    error = "Failed to write launcher: " + launcherPath.string();
    return false;
  }

  const std::string command = mode == LinuxBundleMode::Server ? "serve" : "run";
  launcher << "#!/usr/bin/env sh\n"
           << "set -eu\n"
           << "DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n"
           << "exec " << runtimeCommand << ' ' << command << " --project \"$DIR/project/demi.project.json\" \"$@\"\n";
  launcher.close();
  std::filesystem::permissions(launcherPath,
                               std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec,
                               std::filesystem::perm_options::add,
                               code);
  return true;
}

int buildLinuxBundle(const std::filesystem::path& absoluteProject,
                     const BuildContext& context,
                     const std::vector<std::string>& args,
                     const LinuxBundleMode mode) {
  const std::filesystem::path outputRoot = valueAfter(args, "--output").empty()
                                             ? defaultLinuxOutputRoot(absoluteProject, mode)
                                             : std::filesystem::path(valueAfter(args, "--output"));
  const std::filesystem::path bundledProject = outputRoot / "project";
  std::string error;
  if (!copyProjectDirectory(absoluteProject.parent_path(), bundledProject, mode, error) ||
      !copyRuntimeExecutable(context.executablePath, outputRoot, error) ||
      !writeLinuxLauncher(outputRoot / launcherFileNameFor(absoluteProject, mode),
                          runtimeCommand(),
                          mode,
                          error)) {
    std::cerr << error << '\n';
    return ExitValidationFailure;
  }

  std::cout << "Wrote Linux " << (mode == LinuxBundleMode::Server ? "server " : "")
            << "bundle: " << outputRoot.string() << '\n';
  return ExitSuccess;
}

} // namespace

std::filesystem::path projectFileFromArgs(const std::vector<std::string>& args) {
  const std::string project = valueAfter(args, "--project");
  if (!project.empty()) {
    return project;
  }
  return defaultProjectFile();
}

int runBuildCommand(const std::vector<std::string>& args, const BuildContext& context) {
  if (args.size() < 2) {
    std::cerr << "build requires a target: apk, linux, or linux_server.\n";
    return ExitUsageError;
  }

  const std::filesystem::path project = projectFileFromArgs(args);
  if (project.empty()) {
    std::cerr << "build requires --project <project> or a demi.project.json in the current directory.\n";
    return ExitUsageError;
  }

  const std::filesystem::path absoluteProject = std::filesystem::absolute(project);
  const ValidationSummary summary = validatePath(absoluteProject);
  if (hasErrors(summary.diagnostics)) {
    printDiagnosticsText(std::cerr, summary.diagnostics);
    return ExitValidationFailure;
  }

  if (args[1] == "apk") {
    const std::string gradle = valueAfter(args, "--gradle").empty() ? "gradle" : valueAfter(args, "--gradle");
    const std::filesystem::path androidRoot = context.engineRoot / "android";
    const std::string command = shellQuote(gradle) + " -p " + shellQuote(androidRoot) + " -PdemiProjectFile=" +
                                shellQuote(absoluteProject) + " :app:assembleDebug";
    return std::system(command.c_str()) == 0 ? ExitSuccess : ExitValidationFailure;
  }

  if (args[1] == "linux") {
    return buildLinuxBundle(absoluteProject, context, args, LinuxBundleMode::Game);
  }

  if (args[1] == "linux_server") {
    return buildLinuxBundle(absoluteProject, context, args, LinuxBundleMode::Server);
  }

  std::cerr << "Unknown build target: " << args[1] << '\n';
  return ExitUsageError;
}

} // namespace demi::cli
