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

std::string runtimeCommandFor(const std::string& executablePath) {
  if (executablePath.empty() || executablePath.find('/') == std::string::npos) {
    return "demi";
  }
  return std::filesystem::absolute(executablePath).string();
}

bool shouldSkipBundlePath(const std::filesystem::path& relativePath) {
  if (relativePath.empty()) {
    return false;
  }
  const std::string first = relativePath.begin()->string();
  return first == ".git" || first == "build" || first == "generated";
}

bool copyProjectDirectory(const std::filesystem::path& source, const std::filesystem::path& target, std::string& error) {
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
    if (shouldSkipBundlePath(relativePath)) {
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
    std::cerr << "build requires a target: apk or linux.\n";
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
    const std::filesystem::path outputRoot = valueAfter(args, "--output").empty()
                                                ? std::filesystem::current_path() / "build" / "linux" /
                                                      launcherNameFor(absoluteProject)
                                                : std::filesystem::path(valueAfter(args, "--output"));
    const std::filesystem::path bundledProject = outputRoot / "project";
    std::string error;
    if (!copyProjectDirectory(absoluteProject.parent_path(), bundledProject, error)) {
      std::cerr << error << '\n';
      return ExitValidationFailure;
    }

    const std::filesystem::path launcherPath = outputRoot / launcherNameFor(absoluteProject);
    std::error_code code;
    std::filesystem::create_directories(outputRoot, code);
    std::ofstream launcher(launcherPath);
    if (!launcher) {
      std::cerr << "Failed to write launcher: " << launcherPath.string() << '\n';
      return ExitValidationFailure;
    }
    launcher << "#!/usr/bin/env sh\n"
             << "set -eu\n"
             << "DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n"
             << "exec " << shellQuote(runtimeCommandFor(context.executablePath))
             << " run --project \"$DIR/project/demi.project.json\" \"$@\"\n";
    launcher.close();
    std::filesystem::permissions(launcherPath,
                                 std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_exec,
                                 std::filesystem::perm_options::add,
                                 code);
    std::cout << "Wrote Linux bundle: " << outputRoot.string() << '\n';
    return ExitSuccess;
  }

  std::cerr << "Unknown build target: " << args[1] << '\n';
  return ExitUsageError;
}

} // namespace demi::cli
