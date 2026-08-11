#include "cli/package/PackageCommands.h"

#include "demi/core/Version.h"
#include "demi/packages/PackageArchive.h"
#include "demi/packages/PackageInstaller.h"
#include "demi/packages/PackageRegistry.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scripting/PackageTestRunner.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>

namespace demi::cli::package_commands {
namespace {

constexpr int ExitSuccess = 0;
constexpr int ExitFailure = 1;
constexpr int ExitUsage = 2;

std::string valueAfter(const std::vector<std::string> &args,
                       const std::string &key) {
  for (std::size_t index = 0; index + 1 < args.size(); ++index)
    if (args[index] == key)
      return args[index + 1];
  return {};
}

bool hasArg(const std::vector<std::string> &args, const std::string &key) {
  return std::ranges::find(args, key) != args.end();
}

std::filesystem::path projectFile(const std::vector<std::string> &args) {
  const std::string requested = valueAfter(args, "--project");
  if (!requested.empty()) {
    const std::filesystem::path path(requested);
    return std::filesystem::is_directory(path) ? path / "demi.project.json"
                                               : path;
  }
  return std::filesystem::current_path() / "demi.project.json";
}

std::optional<nlohmann::json> readJson(const std::filesystem::path &path,
                                       Diagnostics &diagnostics) {
  std::ifstream input(path);
  if (!input) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "PACKAGE_PROJECT_READ_FAILED",
                           .message = "Could not read the project file.",
                           .path = path.string()});
    return std::nullopt;
  }
  try {
    nlohmann::json document;
    input >> document;
    return document;
  } catch (const nlohmann::json::exception &exception) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "PACKAGE_PROJECT_INVALID_JSON",
                           .message = exception.what(),
                           .path = path.string()});
    return std::nullopt;
  }
}

std::vector<packages::PackageRequirement>
requirementsFrom(const nlohmann::json &project, Diagnostics &diagnostics,
                 const std::filesystem::path &path) {
  std::vector<packages::PackageRequirement> result;
  const auto values = project.find("packages");
  if (values == project.end())
    return result;
  if (!values->is_object()) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "PROJECT_PACKAGES_INVALID",
                           .message = "Project packages must be an object.",
                           .path = path.string()});
    return result;
  }
  for (const auto &[name, value] : values->items()) {
    const auto constraint =
        value.is_string()
            ? packages::VersionConstraint::parse(value.get<std::string>())
            : std::nullopt;
    if (!packages::validPackageName(name) || !constraint) {
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "PROJECT_PACKAGE_REQUIREMENT_INVALID",
                             .message = "Invalid package requirement: " + name,
                             .path = path.string()});
      continue;
    }
    result.push_back(
        {.name = name, .constraint = *constraint, .requestedBy = "project"});
  }
  return result;
}

std::string registrySource(const std::vector<std::string> &args,
                           const nlohmann::json &project,
                           const PackageCommandContext &context) {
  if (const std::string value = valueAfter(args, "--registry"); !value.empty())
    return value;
  if (const char *value = std::getenv("DEMI_PACKAGE_REGISTRY");
      value != nullptr)
    return value;
  if (const auto value = project.find("package_registry");
      value != project.end() && value->is_string())
    return value->get<std::string>();
  return (context.engineRoot / "packages").string();
}

void print(const Diagnostics &diagnostics, const bool json,
           std::ostream &error) {
  if (json)
    printDiagnosticsJson(error, diagnostics);
  else
    printDiagnosticsText(error, diagnostics);
}

std::optional<std::pair<std::string, std::string>>
parseSpec(const std::string &spec) {
  const std::size_t separator = spec.rfind('@');
  const std::string name = spec.substr(0, separator);
  const std::string constraint =
      separator == std::string::npos ? "*" : spec.substr(separator + 1);
  if (!packages::validPackageName(name) ||
      !packages::VersionConstraint::parse(constraint))
    return std::nullopt;
  return std::pair{name, constraint};
}

std::map<std::string, packages::SemanticVersion>
lockedVersions(const packages::PackageLockLoadResult &lock) {
  std::map<std::string, packages::SemanticVersion> result;
  for (const auto &[name, release] : lock.releases)
    result[name] = release.manifest.version;
  return result;
}

int install(const std::vector<std::string> &args,
            const PackageCommandContext &context, nlohmann::json project,
            const std::filesystem::path &projectPath,
            std::optional<nlohmann::json> replacementProject,
            const bool forceLocked, std::ostream &output, std::ostream &error) {
  Diagnostics diagnostics;
  const bool json = valueAfter(args, "--format") == "json";
  const auto directory = projectPath.parent_path();
  packages::PackageLockLoadResult lock;
  const auto lockPath = directory / packages::PackageLockFilename;
  if (std::filesystem::exists(lockPath))
    lock = packages::loadPackageLock(lockPath);
  if (forceLocked && !std::filesystem::exists(lockPath)) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "PACKAGE_LOCK_REQUIRED",
                           .message = "Locked install requires a lock file.",
                           .path = lockPath.string()});
    print(diagnostics, json, error);
    return ExitFailure;
  }

  const std::string source = forceLocked &&
                                     valueAfter(args, "--registry").empty() &&
                                     !lock.registry.empty()
                                 ? lock.registry
                                 : registrySource(args, project, context);
  auto registry = packages::makePackageRegistry(source, diagnostics, directory);
  if (!registry) {
    print(diagnostics, json, error);
    return ExitFailure;
  }
  std::map<std::string, packages::PackageRelease> releases;
  if (forceLocked) {
    diagnostics.insert(diagnostics.end(), lock.diagnostics.begin(),
                       lock.diagnostics.end());
    releases = lock.releases;
    for (const auto &[name, release] : releases)
      if (release.yanked)
        diagnostics.push_back(
            {.severity = Severity::Error,
             .code = "PACKAGE_LOCKED_VERSION_YANKED",
             .message = "Locked package version is marked yanked: " + name +
                        "@" + release.manifest.version.string(),
             .path = lockPath.string(),
             .suggestion = "Run demi package update after reviewing the "
                           "replacement version."});
  } else {
    const auto requirements =
        requirementsFrom(replacementProject ? *replacementProject : project,
                         diagnostics, projectPath);
    const auto engine = packages::SemanticVersion::parse(EngineVersion);
    auto pins = hasArg(args, "--preserve-locked")
                    ? lockedVersions(lock)
                    : std::map<std::string, packages::SemanticVersion>{};
    if (const std::string updated = valueAfter(args, "--update-only");
        !updated.empty()) {
      pins = lockedVersions(lock);
      pins.erase(updated);
    }
    auto resolution =
        packages::resolvePackages(*registry, requirements, *engine, pins);
    diagnostics.insert(diagnostics.end(), resolution.diagnostics.begin(),
                       resolution.diagnostics.end());
    releases = std::move(resolution.selected);
  }
  if (hasErrors(diagnostics)) {
    print(diagnostics, json, error);
    return ExitFailure;
  }
  auto installDiagnostics = packages::installPackages(
      *registry, releases,
      {.projectDirectory = directory,
       .cacheDirectory = valueAfter(args, "--cache"),
       .offline = hasArg(args, "--offline"),
       .dryRun = hasArg(args, "--dry-run"),
       .replacementProject = replacementProject,
       .validateStaging = [](const std::filesystem::path &staging) {
         Diagnostics result;
         for (const auto &entry :
              std::filesystem::directory_iterator(staging)) {
           if (!entry.is_directory())
             continue;
           const auto loaded = packages::loadPackageManifest(
               entry.path() / packages::PackageManifestFilename);
           result.insert(result.end(), loaded.diagnostics.begin(),
                         loaded.diagnostics.end());
           if (!loaded.manifest)
             continue;
           for (const auto &file : loaded.manifest->files)
             if (file.starts_with("scripts/") && file.ends_with(".lua")) {
               auto scriptDiagnostics =
                   runtime::LuaScriptHost::checkScriptSyntax(entry.path() /
                                                             file);
               result.insert(result.end(), scriptDiagnostics.begin(),
                             scriptDiagnostics.end());
             }
         }
         return result;
       }});
  diagnostics.insert(diagnostics.end(), installDiagnostics.begin(),
                     installDiagnostics.end());
  if (hasErrors(diagnostics)) {
    print(diagnostics, json, error);
    return ExitFailure;
  }
  if (json) {
    nlohmann::json installed = nlohmann::json::array();
    for (const auto &[name, release] : releases)
      installed.push_back(
          {{"name", name}, {"version", release.manifest.version.string()}});
    output << nlohmann::json{{"status", hasArg(args, "--dry-run")
                                            ? "planned"
                                            : "installed"},
                             {"packages", installed}}
                  .dump(2)
           << '\n';
  } else {
    output << (hasArg(args, "--dry-run") ? "Would install " : "Installed ")
           << releases.size() << " package(s).\n";
  }
  return ExitSuccess;
}

} // namespace

int runPackageCommand(const std::vector<std::string> &args,
                      const PackageCommandContext &context,
                      std::ostream &output, std::ostream &error) {
  if (args.size() < 2) {
    error << "package requires a subcommand.\n";
    return ExitUsage;
  }
  const std::string &command = args[1];
  const bool json = valueAfter(args, "--format") == "json";
  if (command == "test") {
    const std::filesystem::path root =
        args.size() >= 3 && !args[2].starts_with("--")
            ? std::filesystem::path(args[2])
            : std::filesystem::current_path();
    const auto loaded =
        packages::loadPackageManifest(root / packages::PackageManifestFilename);
    if (!loaded.manifest) {
      print(loaded.diagnostics, json, error);
      return ExitFailure;
    }
    std::vector<std::filesystem::path> tests;
    for (const auto &file : loaded.manifest->files)
      if (file.starts_with("tests/") && file.ends_with(".lua"))
        tests.emplace_back(file);
    const auto result = runtime::runPackageTests(root, tests);
    if (json) {
      output << nlohmann::json{{"passed", result.passed},
                               {"failed", result.failed},
                               {"failures", result.failures}}
                    .dump(2)
             << '\n';
    } else {
      for (const auto &failure : result.failures)
        error << "FAIL " << failure << '\n';
      output << result.passed << " passed, " << result.failed << " failed.\n";
    }
    if (hasErrors(result.diagnostics))
      print(result.diagnostics, json, error);
    return result.failed == 0 && !hasErrors(result.diagnostics) ? ExitSuccess
                                                                : ExitFailure;
  }
  if (command == "publish") {
    const std::filesystem::path root =
        args.size() >= 3 && !args[2].starts_with("--")
            ? std::filesystem::path(args[2])
            : std::filesystem::current_path();
    Diagnostics diagnostics;
    const auto manifest =
        packages::loadPackageManifest(root / packages::PackageManifestFilename);
    diagnostics.insert(diagnostics.end(), manifest.diagnostics.begin(),
                       manifest.diagnostics.end());
    if (!manifest.manifest) {
      print(diagnostics, json, error);
      return ExitFailure;
    }
    const auto archive = std::filesystem::temp_directory_path() /
                         (manifest.manifest->name + "-" +
                          manifest.manifest->version.string() + ".demipkg");
    auto archiveDiagnostics = packages::createPackageArchive(root, archive);
    diagnostics.insert(diagnostics.end(), archiveDiagnostics.begin(),
                       archiveDiagnostics.end());
    auto info = packages::inspectPackageArchive(archive, diagnostics);
    const std::string source = !valueAfter(args, "--registry").empty()
                                   ? valueAfter(args, "--registry")
                                   : (context.engineRoot / "packages").string();
    auto registry = packages::makePackageRegistry(
        source, diagnostics, std::filesystem::current_path());
    if (info && registry)
      (void)registry->publish(archive, *info, diagnostics);
    std::error_code removeError;
    std::filesystem::remove(archive, removeError);
    if (hasErrors(diagnostics)) {
      print(diagnostics, json, error);
      return ExitFailure;
    }
    if (json)
      output << nlohmann::json{{"status", "published"},
                               {"name", info->manifest.name},
                               {"version", info->manifest.version.string()},
                               {"registry", source}}
                    .dump(2)
             << '\n';
    else
      output << "Published " << info->manifest.name << '@'
             << info->manifest.version.string() << " to " << source << ".\n";
    return ExitSuccess;
  }

  const auto projectPath = projectFile(args);
  Diagnostics diagnostics;
  auto project = readJson(projectPath, diagnostics);
  if (!project) {
    print(diagnostics, json, error);
    return ExitFailure;
  }
  if (command == "add") {
    if (args.size() < 3 || args[2].starts_with("--")) {
      error << "Usage: demi package add <name>@<constraint> --project "
               "<project>\n";
      return ExitUsage;
    }
    const auto spec = parseSpec(args[2]);
    if (!spec) {
      error << "Invalid package name or semantic-version constraint.\n";
      return ExitUsage;
    }
    nlohmann::json replacement = *project;
    if (!replacement.contains("packages") ||
        !replacement["packages"].is_object())
      replacement["packages"] = nlohmann::json::object();
    replacement["packages"][spec->first] = spec->second;
    return install(args, context, *project, projectPath, std::move(replacement),
                   false, output, error);
  }
  if (command == "remove") {
    if (args.size() < 3 || !packages::validPackageName(args[2])) {
      error << "Usage: demi package remove <name> --project <project>\n";
      return ExitUsage;
    }
    nlohmann::json replacement = *project;
    if (replacement.contains("packages") && replacement["packages"].is_object())
      replacement["packages"].erase(args[2]);
    return install(args, context, *project, projectPath, std::move(replacement),
                   false, output, error);
  }
  if (command == "install")
    return install(args, context, *project, projectPath, std::nullopt,
                   hasArg(args, "--locked"), output, error);
  if (command == "update") {
    nlohmann::json replacement = *project;
    if (args.size() >= 3 && !args[2].starts_with("--")) {
      if (!replacement.contains("packages") ||
          !replacement["packages"].is_object() ||
          !replacement["packages"].contains(args[2])) {
        error << "Direct package is not declared: " << args[2] << '\n';
        return ExitUsage;
      }
    }
    auto updateArgs = args;
    if (args.size() >= 3 && !args[2].starts_with("--")) {
      updateArgs.push_back("--update-only");
      updateArgs.push_back(args[2]);
    }
    return install(updateArgs, context, *project, projectPath, std::nullopt,
                   false, output, error);
  }
  if (command == "list") {
    const auto lock = packages::loadPackageLock(projectPath.parent_path() /
                                                packages::PackageLockFilename);
    if (hasErrors(lock.diagnostics)) {
      print(lock.diagnostics, json, error);
      return ExitFailure;
    }
    if (json)
      output << packages::packageLockJson(lock.releases, lock.registry).dump(2)
             << '\n';
    else
      for (const auto &[name, release] : lock.releases)
        output << name << ' ' << release.manifest.version.string() << '\n';
    return ExitSuccess;
  }
  if (command == "outdated") {
    auto requirements = requirementsFrom(*project, diagnostics, projectPath);
    const std::string source = registrySource(args, *project, context);
    auto registry = packages::makePackageRegistry(source, diagnostics,
                                                  projectPath.parent_path());
    if (!registry || hasErrors(diagnostics)) {
      print(diagnostics, json, error);
      return ExitFailure;
    }
    const auto lock = packages::loadPackageLock(projectPath.parent_path() /
                                                packages::PackageLockFilename);
    nlohmann::json outdated = nlohmann::json::array();
    for (const auto &requirement : requirements) {
      auto releases = registry->releases(requirement.name, diagnostics);
      std::ranges::sort(releases, [](const auto &left, const auto &right) {
        return left.manifest.version > right.manifest.version;
      });
      const auto latest =
          std::ranges::find_if(releases, [&](const auto &release) {
            return !release.yanked &&
                   requirement.constraint.accepts(release.manifest.version);
          });
      const auto current = lock.releases.find(requirement.name);
      if (latest != releases.end() &&
          (current == lock.releases.end() ||
           current->second.manifest.version < latest->manifest.version)) {
        const std::string currentVersion =
            current == lock.releases.end()
                ? "not-installed"
                : current->second.manifest.version.string();
        if (json)
          outdated.push_back({{"name", requirement.name},
                              {"current", currentVersion},
                              {"latest", latest->manifest.version.string()}});
        else
          output << requirement.name << ' ' << currentVersion << " -> "
                 << latest->manifest.version.string() << '\n';
      }
    }
    if (hasErrors(diagnostics)) {
      print(diagnostics, json, error);
      return ExitFailure;
    }
    if (json)
      output << nlohmann::json{{"packages", std::move(outdated)}}.dump(2)
             << '\n';
    return ExitSuccess;
  }
  error << "Unknown package subcommand: " << command << '\n';
  return ExitUsage;
}

} // namespace demi::cli::package_commands
