#include "cli/build/LinuxPackaging.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/core/Version.h"
#include "demi/runtime/scene/ProjectBuildSettings.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace demi::build {
namespace {

void add(Diagnostics &diagnostics, std::string code, std::string message,
         const std::filesystem::path &path = {}) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path.string(),
                         .suggestion = {}});
}

std::optional<runtime::ProjectBuildSettings>
settingsFor(const std::filesystem::path &projectFile) {
  try {
    std::ifstream input(projectFile);
    if (!input)
      return std::nullopt;
    const auto parsed = runtime::parseProjectBuildSettings(
        nlohmann::json::parse(input), projectFile);
    return hasErrors(parsed.diagnostics)
               ? std::nullopt
               : std::make_optional(parsed.settings);
  } catch (const nlohmann::json::exception &) {
    return std::nullopt;
  }
}

std::string singleLine(std::string value) {
  std::ranges::replace(value, '\n', ' ');
  std::ranges::replace(value, '\r', ' ');
  return value;
}

bool copyRuntime(const std::filesystem::path &source,
                 const std::filesystem::path &target,
                 Diagnostics &diagnostics) {
  if (!std::filesystem::is_regular_file(source)) {
    add(diagnostics, "BUILD_RUNTIME_NOT_FOUND",
        "The Demi runtime executable was not found.", source);
    return false;
  }
  std::error_code error;
  std::filesystem::create_directories(target.parent_path(), error);
  if (!error)
    std::filesystem::copy_file(
        source, target, std::filesystem::copy_options::overwrite_existing,
        error);
  if (error) {
    add(diagnostics, "BUILD_RUNTIME_COPY_FAILED", error.message(), target);
    return false;
  }
  std::filesystem::permissions(target,
                               std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec,
                               std::filesystem::perm_options::add, error);
  return !error;
}

bool writeLauncher(const LinuxPackageRequest &request,
                   const std::string &executableName,
                   Diagnostics &diagnostics) {
  const auto path = request.stagingDirectory /
                    (request.server ? executableName + "-server"
                                    : executableName);
  std::ofstream launcher(path);
  if (!launcher) {
    add(diagnostics, "BUILD_LAUNCHER_WRITE_FAILED",
        "Could not write the Linux launcher.", path);
    return false;
  }
  launcher << "#!/usr/bin/env sh\n"
              "set -eu\n"
              "DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n"
              "exec \"$DIR/bin/"
           << executableName << "\" " << (request.server ? "serve" : "run")
           << " --project \"$DIR/project/demi.project.json\" \"$@\"\n";
  launcher.close();
  std::error_code error;
  std::filesystem::permissions(path,
                               std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec,
                               std::filesystem::perm_options::add, error);
  return !error;
}

std::string stageIcon(const LinuxPackageRequest &request,
                      const runtime::ProjectBuildSettings &settings,
                      Diagnostics &diagnostics) {
  if (settings.icon.empty() || request.server)
    return {};
  const AssetRegistry registry =
      loadAssetRegistry(request.projectFile.parent_path());
  const AssetManifest *icon = findAsset(registry, settings.icon);
  if (icon == nullptr || !std::filesystem::is_regular_file(icon->sourcePath)) {
    add(diagnostics, "BUILD_LINUX_ICON_NOT_FOUND",
        "The configured Linux application icon was not found.",
        request.projectFile);
    return {};
  }
  const std::string extension = icon->sourcePath.extension().string();
  const auto directory = request.stagingDirectory / "share/icons/hicolor" /
                         (extension == ".svg" ? "scalable/apps"
                                              : "512x512/apps");
  const auto target = directory / (settings.applicationId + extension);
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (!error)
    std::filesystem::copy_file(
        icon->sourcePath, target,
        std::filesystem::copy_options::overwrite_existing, error);
  if (error) {
    add(diagnostics, "BUILD_LINUX_ICON_COPY_FAILED", error.message(), target);
    return {};
  }
  return settings.applicationId;
}

bool writeDesktopEntry(const LinuxPackageRequest &request,
                       const runtime::ProjectBuildSettings &settings,
                       const std::string &icon, Diagnostics &diagnostics) {
  if (request.server)
    return true;
  const auto path = request.stagingDirectory / "share/applications" /
                    (settings.applicationId + ".desktop");
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream output(path);
  if (error || !output) {
    add(diagnostics, "BUILD_LINUX_DESKTOP_WRITE_FAILED",
        error ? error.message() : "Could not open desktop entry.", path);
    return false;
  }
  output << "[Desktop Entry]\nType=Application\nName="
         << singleLine(settings.displayName) << "\nExec="
         << settings.executableName << "\nTerminal=false\nCategories=Game;\n";
  if (!icon.empty())
    output << "Icon=" << icon << '\n';
  return true;
}

bool writeNotices(const LinuxPackageRequest &request,
                  const runtime::ProjectBuildSettings &settings,
                  Diagnostics &diagnostics) {
  const auto directory = request.stagingDirectory / "share/doc" /
                         settings.executableName;
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  std::ofstream output(directory / "THIRD_PARTY_NOTICES.txt");
  if (error || !output) {
    add(diagnostics, "BUILD_LINUX_NOTICES_WRITE_FAILED",
        error ? error.message() : "Could not write notices.", directory);
    return false;
  }
  output << "DemiEngine " << EngineBuildVersion << "\n"
         << EngineRepositoryUrl << "\n\n";
  AssetRegistry registry = loadAssetRegistry(request.projectFile.parent_path());
  std::ranges::sort(registry.assets, {}, &AssetManifest::id);
  std::set<std::filesystem::path> includedLicenses;
  for (const AssetManifest &asset : registry.assets) {
    if (asset.attribution.empty() && !asset.licensePath)
      continue;
    output << asset.id << '\n';
    if (!asset.attribution.empty())
      output << singleLine(asset.attribution) << '\n';
    if (asset.licensePath && includedLicenses.insert(*asset.licensePath).second) {
      std::ifstream license(*asset.licensePath);
      output << license.rdbuf() << '\n';
    }
    output << '\n';
  }
  return true;
}

bool writeReport(const LinuxPackageRequest &request,
                 const runtime::ProjectBuildSettings &settings,
                 const std::filesystem::path &runtime,
                 Diagnostics &diagnostics) {
  const auto runtimeHash = assets::hashFile(runtime);
  const auto projectHash = assets::hashFile(request.projectFile);
  const auto cookHash = assets::hashFile(
      request.stagingDirectory / "project/cook.manifest.json");
  if (!runtimeHash || !projectHash || !cookHash) {
    add(diagnostics, "BUILD_LINUX_REPORT_HASH_FAILED",
        "Could not hash all Linux build report inputs.",
        request.stagingDirectory);
    return false;
  }
  const nlohmann::json report{
      {"format_version", 1},
      {"platform", request.server ? "linux_server" : "linux"},
      {"engine_version", std::string(EngineBuildVersion)},
      {"application_id", settings.applicationId},
      {"version_name", settings.versionName},
      {"executable", settings.executableName},
      {"runtime_hash", *runtimeHash},
      {"project_hash", *projectHash},
      {"cook_manifest_hash", *cookHash},
      {"shared_library_policy", "system"},
      {"writable_paths", {"XDG_DATA_HOME", "XDG_CACHE_HOME"}}};
  std::ofstream output(request.stagingDirectory / "build-report.json");
  if (!output) {
    add(diagnostics, "BUILD_LINUX_REPORT_WRITE_FAILED",
        "Could not write the Linux build report.", request.stagingDirectory);
    return false;
  }
  output << report.dump(2) << '\n';
  return true;
}

} // namespace

Diagnostics stageLinuxPackage(const LinuxPackageRequest &request) {
  Diagnostics diagnostics;
  const auto parsedSettings = settingsFor(request.projectFile);
  if (!parsedSettings) {
    add(diagnostics, "BUILD_LINUX_SETTINGS_READ_FAILED",
        "Could not read validated Linux project settings.",
        request.projectFile);
    return diagnostics;
  }
  runtime::ProjectBuildSettings settings = *parsedSettings;
  if (settings.applicationId.empty())
    settings.applicationId = "dev.demi." + settings.executableName;
  std::error_code cleanupError;
  const auto cookCache = request.stagingDirectory / "project/.cook-cache";
  const auto cacheStatus = std::filesystem::symlink_status(cookCache,
                                                           cleanupError);
  if (cleanupError == std::errc::no_such_file_or_directory)
    cleanupError.clear();
  if (cleanupError) {
    add(diagnostics, "BUILD_LINUX_CACHE_INSPECT_FAILED",
        cleanupError.message(), cookCache);
    return diagnostics;
  }
  if (std::filesystem::is_symlink(cacheStatus)) {
    add(diagnostics, "BUILD_LINUX_CACHE_OUTPUT_UNSAFE",
        "Cook cache staging must not be a symbolic link.", cookCache);
    return diagnostics;
  }
  if (std::filesystem::is_directory(cacheStatus))
    std::filesystem::remove_all(cookCache, cleanupError);
  if (cleanupError) {
    add(diagnostics, "BUILD_LINUX_CACHE_REMOVE_FAILED",
        cleanupError.message(), cookCache);
    return diagnostics;
  }
  const auto runtime =
      request.stagingDirectory / "bin" / settings.executableName;
  if (!copyRuntime(request.runtimeExecutable, runtime, diagnostics) ||
      !writeLauncher(request, settings.executableName, diagnostics))
    return diagnostics;
  const std::string icon = stageIcon(request, settings, diagnostics);
  if (hasErrors(diagnostics) ||
      !writeDesktopEntry(request, settings, icon, diagnostics) ||
      !writeNotices(request, settings, diagnostics) ||
      !writeReport(request, settings, runtime, diagnostics))
    return diagnostics;
  return diagnostics;
}

} // namespace demi::build
