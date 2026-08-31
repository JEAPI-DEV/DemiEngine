#include "cli/build/BuildService.h"
#include "cli/build/LinuxPackaging.h"

#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetHash.h"
#include "demi/core/Version.h"
#include "demi/runtime/scene/ProjectBuildSettings.h"
#include "demi/schema/Validation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <system_error>

#if defined(__linux__)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
#endif

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

bool cancelled(const ProjectOperationRequest &request) {
  return request.isCancelled && request.isCancelled();
}

void report(const ProjectOperationRequest &request,
            const ProjectOperationStage stage, const float fraction,
            std::string message) {
  if (request.reportProgress)
    request.reportProgress(
        {.stage = stage, .fraction = fraction, .message = std::move(message)});
}

std::filesystem::path uniqueSibling(const std::filesystem::path &target,
                                    const std::string_view suffix) {
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return target.parent_path() /
         ("." + target.filename().string() + "." + std::string(suffix) + "." +
          std::to_string(stamp));
}

struct TemporaryDirectory {
  std::filesystem::path path;
  ~TemporaryDirectory() {
    if (path.empty())
      return;
    std::error_code ignored;
    const auto status = std::filesystem::symlink_status(path, ignored);
    if (!ignored && !std::filesystem::is_symlink(status))
      std::filesystem::remove_all(path, ignored);
  }
};

bool commitDirectory(const std::filesystem::path &staging,
                     const std::filesystem::path &target, Diagnostics &issues) {
  std::error_code error;
  std::filesystem::create_directories(target.parent_path(), error);
  if (error) {
    add(issues, "BUILD_OUTPUT_PARENT_FAILED", error.message(),
        target.parent_path());
    return false;
  }
  const std::filesystem::path backup = uniqueSibling(target, "previous");
  const bool hadTarget = std::filesystem::exists(target, error);
  if (error) {
    add(issues, "BUILD_OUTPUT_INSPECT_FAILED", error.message(), target);
    return false;
  }
  if (hadTarget) {
    const auto status = std::filesystem::symlink_status(target, error);
    if (error || !std::filesystem::is_directory(status) ||
        std::filesystem::is_symlink(status)) {
      add(issues, "BUILD_OUTPUT_UNSAFE",
          "Existing build output must be a real directory.", target);
      return false;
    }
    std::filesystem::rename(target, backup, error);
    if (error) {
      add(issues, "BUILD_OUTPUT_BACKUP_FAILED", error.message(), target);
      return false;
    }
  }
  std::filesystem::rename(staging, target, error);
  if (error) {
    if (hadTarget) {
      std::error_code restoreError;
      std::filesystem::rename(backup, target, restoreError);
    }
    add(issues, "BUILD_OUTPUT_COMMIT_FAILED", error.message(), target);
    return false;
  }
  if (hadTarget)
    std::filesystem::remove_all(backup, error);
  return true;
}

bool publishFile(const std::filesystem::path &source,
                 const std::filesystem::path &target, Diagnostics &issues) {
  std::error_code error;
  std::filesystem::create_directories(target.parent_path(), error);
  if (error) {
    add(issues, "BUILD_ARTIFACT_DIRECTORY_FAILED", error.message(),
        target.parent_path());
    return false;
  }

  const std::filesystem::path staging = uniqueSibling(target, "staging");
  std::filesystem::copy_file(source, staging,
                             std::filesystem::copy_options::overwrite_existing,
                             error);
  if (error) {
    add(issues, "BUILD_ARTIFACT_COPY_FAILED", error.message(), target);
    return false;
  }

  const std::filesystem::path backup = uniqueSibling(target, "previous");
  const bool hadTarget = std::filesystem::exists(target, error);
  if (error) {
    const std::string message = error.message();
    std::error_code cleanupError;
    std::filesystem::remove(staging, cleanupError);
    add(issues, "BUILD_ARTIFACT_INSPECT_FAILED", message, target);
    return false;
  }
  std::filesystem::file_status targetStatus;
  if (hadTarget)
    targetStatus = std::filesystem::symlink_status(target, error);
  if (error) {
    const std::string message = error.message();
    std::error_code cleanupError;
    std::filesystem::remove(staging, cleanupError);
    add(issues, "BUILD_ARTIFACT_INSPECT_FAILED", message, target);
    return false;
  }
  if (hadTarget && (!std::filesystem::is_regular_file(targetStatus) ||
                    std::filesystem::is_symlink(targetStatus))) {
    std::error_code cleanupError;
    std::filesystem::remove(staging, cleanupError);
    add(issues, "BUILD_ARTIFACT_OUTPUT_UNSAFE",
        "Existing artifact output must be a regular file, not a directory or "
        "symbolic link.",
        target);
    return false;
  }
  if (hadTarget) {
    std::filesystem::rename(target, backup, error);
    if (error) {
      const std::string message = error.message();
      std::error_code cleanupError;
      std::filesystem::remove(staging, cleanupError);
      add(issues, "BUILD_ARTIFACT_BACKUP_FAILED", message, target);
      return false;
    }
  }
  std::filesystem::rename(staging, target, error);
  if (error) {
    if (hadTarget) {
      std::error_code restoreError;
      std::filesystem::rename(backup, target, restoreError);
    }
    add(issues, "BUILD_ARTIFACT_PUBLISH_FAILED", error.message(), target);
    return false;
  }
  if (hadTarget)
    std::filesystem::remove(backup, error);
  return true;
}

std::optional<runtime::ProjectBuildSettings>
loadProjectBuildSettings(const std::filesystem::path &projectFile) {
  try {
    std::ifstream input(projectFile);
    if (!input)
      return std::nullopt;
    const auto document = nlohmann::json::parse(input);
    const auto parsed =
        runtime::parseProjectBuildSettings(document, projectFile);
    return hasErrors(parsed.diagnostics)
               ? std::nullopt
               : std::make_optional(parsed.settings);
  } catch (const nlohmann::json::exception &) {
    return std::nullopt;
  }
}

struct AndroidPackageVariant {
  std::string configuration;
  std::string kind;
  std::string gradleTask;
  std::filesystem::path gradleArtifact;
  std::string outputSuffix;
  bool requiresSigning = false;
};

std::optional<AndroidPackageVariant>
androidPackageVariant(const ProjectOperation operation) {
  switch (operation) {
  case ProjectOperation::PackageAndroid:
    return AndroidPackageVariant{
        .configuration = "debug",
        .kind = "apk",
        .gradleTask = ":app:demiAssembleDebug",
        .gradleArtifact = "app/build/outputs/apk/debug/app-debug.apk",
        .outputSuffix = "-debug.apk"};
  case ProjectOperation::PackageAndroidRelease:
    return AndroidPackageVariant{
        .configuration = "release",
        .kind = "apk",
        .gradleTask = ":app:demiAssembleRelease",
        .gradleArtifact = "app/build/outputs/apk/release/app-release.apk",
        .outputSuffix = "-release.apk",
        .requiresSigning = true};
  case ProjectOperation::BundleAndroidRelease:
    return AndroidPackageVariant{
        .configuration = "release",
        .kind = "aab",
        .gradleTask = ":app:demiBundleRelease",
        .gradleArtifact = "app/build/outputs/bundle/release/app-release.aab",
        .outputSuffix = "-release.aab",
        .requiresSigning = true};
  default:
    return std::nullopt;
  }
}

bool validateAndroidSigningEnvironment(const AndroidPackageVariant &variant,
                                       Diagnostics &diagnostics) {
  if (!variant.requiresSigning)
    return true;
  constexpr std::array Names{"DEMI_ANDROID_KEYSTORE",
                             "DEMI_ANDROID_KEYSTORE_PASSWORD",
                             "DEMI_ANDROID_KEY_ALIAS",
                             "DEMI_ANDROID_KEY_PASSWORD"};
  std::vector<std::string> missing;
  for (const char *name : Names) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0')
      missing.emplace_back(name);
  }
  if (!missing.empty()) {
    std::string message = "Android release signing requires environment "
                          "variables: ";
    for (std::size_t index = 0; index < missing.size(); ++index) {
      if (index != 0)
        message += ", ";
      message += missing[index];
    }
    add(diagnostics, "BUILD_ANDROID_SIGNING_ENVIRONMENT_MISSING",
        std::move(message));
    return false;
  }
  const std::filesystem::path keystore =
      std::getenv("DEMI_ANDROID_KEYSTORE");
  if (!std::filesystem::is_regular_file(keystore)) {
    add(diagnostics, "BUILD_ANDROID_KEYSTORE_NOT_FOUND",
        "The keystore referenced by DEMI_ANDROID_KEYSTORE does not exist.",
        keystore);
    return false;
  }
  return true;
}

bool writeAndroidPackageReport(
    const ProjectOperationRequest &request,
    const AndroidPackageVariant &variant,
    const runtime::ProjectBuildSettings &settings,
    const std::filesystem::path &cookedProject,
    const std::filesystem::path &artifact, Diagnostics &diagnostics) {
  const auto artifactHash = assets::hashFile(artifact);
  const auto projectHash = assets::hashFile(request.projectFile);
  const auto cookManifestHash =
      assets::hashFile(cookedProject.parent_path() / "cook.manifest.json");
  if (!artifactHash || !projectHash || !cookManifestHash) {
    add(diagnostics, "BUILD_ANDROID_REPORT_HASH_FAILED",
        "Could not hash all Android build report inputs.", artifact);
    return false;
  }
  const nlohmann::json report{
      {"format_version", 1},
      {"platform", "android"},
      {"configuration", variant.configuration},
      {"kind", variant.kind},
      {"engine_version", std::string(EngineBuildVersion)},
      {"application_id", settings.applicationId},
      {"version_name", settings.versionName},
      {"version_code", settings.versionCode},
      {"min_sdk", settings.android.minimumSdk},
      {"target_sdk", 36},
      {"abis", settings.android.abis},
      {"permissions", settings.android.permissions},
      {"graphics_backends", {"vulkan", "opengles"}},
      {"artifact", artifact.filename().generic_string()},
      {"artifact_hash", *artifactHash},
      {"project_hash", *projectHash},
      {"cook_manifest_hash", *cookManifestHash}};
  std::filesystem::path reportPath = artifact;
  reportPath += ".build-report.json";
  std::ofstream output(reportPath);
  if (!output) {
    add(diagnostics, "BUILD_ANDROID_REPORT_WRITE_FAILED",
        "Could not write the Android build report.", reportPath);
    return false;
  }
  output << report.dump(2) << '\n';
  return true;
}

bool cookTransactionally(const ProjectOperationRequest &request,
                         const std::filesystem::path &output,
                         const std::string &platform,
                         Diagnostics &diagnostics) {
  const std::filesystem::path staging = uniqueSibling(output, "staging");
  TemporaryDirectory cleanup{staging};
  const Diagnostics cooked =
      assets::cookProject({.projectFile = request.projectFile,
                           .outputDirectory = staging,
                           .platform = platform,
                           .shaderCompiler = {},
                           .shaderIncludeDirectory = {}});
  diagnostics.insert(diagnostics.end(), cooked.begin(), cooked.end());
  if (hasErrors(diagnostics) || cancelled(request))
    return false;
  if (!commitDirectory(staging, output, diagnostics))
    return false;
  cleanup.path.clear();
  return true;
}

#if defined(__linux__)
bool runAndroidGradle(const ProjectOperationRequest &request,
                      const std::filesystem::path &packagedProject,
                      const AndroidPackageVariant &variant,
                      Diagnostics &issues) {
  const auto androidRoot = request.engineRoot / "android";
  const auto completionMarker = androidRoot / "app/build/generated/demi" /
                                (variant.configuration == "debug"
                                     ? "package-debug-complete.txt"
                                     : "package-complete.txt");
  const auto progressFile =
      androidRoot / "app/build/generated/demi/package-progress.json";
  const std::string buildToken = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string projectProperty =
      "-PdemiProjectFile=" + packagedProject.string();
  const std::string tokenProperty = "-PdemiBuildToken=" + buildToken;
  const std::string progressProperty =
      "-PdemiProgressFile=" + progressFile.string();
  const std::string androidRootString = androidRoot.string();
  std::error_code filesystemError;
  (void)std::filesystem::remove(completionMarker, filesystemError);
  if (filesystemError) {
    add(issues, "BUILD_ANDROID_MARKER_REMOVE_FAILED",
        "Could not clear the previous Android completion marker: " +
            filesystemError.message(),
        completionMarker);
    return false;
  }
  (void)std::filesystem::remove(progressFile, filesystemError);
  if (filesystemError) {
    add(issues, "BUILD_ANDROID_PROGRESS_REMOVE_FAILED",
        "Could not clear the previous Android progress file: " +
            filesystemError.message(),
        progressFile);
    return false;
  }

  std::array<char *, 11> arguments{
      const_cast<char *>(request.gradleExecutable.c_str()),
      const_cast<char *>("--no-daemon"),
      const_cast<char *>("--console=plain"),
      const_cast<char *>("--no-watch-fs"),
      const_cast<char *>("-p"),
      const_cast<char *>(androidRootString.c_str()),
      const_cast<char *>(projectProperty.c_str()),
      const_cast<char *>(tokenProperty.c_str()),
      const_cast<char *>(progressProperty.c_str()),
      const_cast<char *>(variant.gradleTask.c_str()),
      nullptr};

  posix_spawnattr_t attributes;
  if (posix_spawnattr_init(&attributes) != 0) {
    add(issues, "BUILD_ANDROID_PROCESS_FAILED",
        "Could not initialize the Android build process.", androidRoot);
    return false;
  }
  (void)posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  (void)posix_spawnattr_setpgroup(&attributes, 0);

  posix_spawn_file_actions_t fileActions;
  const int actionsResult = posix_spawn_file_actions_init(&fileActions);
  if (actionsResult != 0) {
    (void)posix_spawnattr_destroy(&attributes);
    add(issues, "BUILD_ANDROID_PROCESS_FAILED",
        "Could not initialize Android process IO: " +
            std::error_code(actionsResult, std::generic_category()).message(),
        androidRoot);
    return false;
  }
  (void)posix_spawn_file_actions_addopen(&fileActions, STDIN_FILENO,
                                         "/dev/null", O_RDONLY, 0);

  pid_t child = 0;
  const int spawnResult =
      posix_spawnp(&child, request.gradleExecutable.c_str(), &fileActions,
                   &attributes, arguments.data(), environ);
  (void)posix_spawn_file_actions_destroy(&fileActions);
  (void)posix_spawnattr_destroy(&attributes);
  if (spawnResult != 0) {
    add(issues, "BUILD_ANDROID_PROCESS_FAILED",
        "Could not start the Android build process: " +
            std::error_code(spawnResult, std::generic_category()).message(),
        androidRoot);
    return false;
  }

  std::optional<std::chrono::steady_clock::time_point> markerObserved;
  bool completedFromMarker = false;
  int reportedCompleted = -1;
  std::string reportedTask;
  int status = 0;
  while (true) {
    const pid_t waitResult = waitpid(child, &status, WNOHANG);
    if (waitResult == child)
      break;
    if (waitResult < 0) {
      if (errno == EINTR)
        continue;
      add(issues, "BUILD_ANDROID_WAIT_FAILED",
          "Could not wait for the Android build process: " +
              std::error_code(errno, std::generic_category()).message(),
          androidRoot);
      return false;
    }

    std::ifstream markerInput(completionMarker);
    std::string markerToken;
    if (markerInput && std::getline(markerInput, markerToken) &&
        markerToken == buildToken) {
      if (!markerObserved) {
        markerObserved = std::chrono::steady_clock::now();
        report(request, ProjectOperationStage::Package, 0.96F,
               "Gradle build complete; stopping its launcher");
      } else if (std::chrono::steady_clock::now() - *markerObserved >=
                 std::chrono::seconds(2)) {
        (void)kill(-child, SIGTERM);
        const auto stopDeadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < stopDeadline) {
          const pid_t stopped = waitpid(child, &status, WNOHANG);
          if (stopped == child)
            break;
          if (stopped < 0 && errno != EINTR)
            break;
          usleep(50'000);
        }
        if (waitpid(child, &status, WNOHANG) == 0) {
          (void)kill(-child, SIGKILL);
          (void)waitpid(child, &status, 0);
        }
        completedFromMarker = true;
        break;
      }
    }
    if (cancelled(request)) {
      (void)kill(-child, SIGTERM);
      (void)waitpid(child, &status, 0);
      return false;
    }

    try {
      std::ifstream progressInput(progressFile);
      if (progressInput) {
        const auto progress = nlohmann::json::parse(progressInput);
        if (progress.value("token", "") == buildToken) {
          const int total = progress.value("total", 0);
          const int completed = progress.value("completed", 0);
          const std::string current = progress.value("current", "Gradle");
          if (total > 0 &&
              (completed != reportedCompleted || current != reportedTask)) {
            reportedCompleted = completed;
            reportedTask = current;
            const float taskFraction = std::clamp(
                static_cast<float>(completed) / static_cast<float>(total),
                0.0F, 1.0F);
            report(request, ProjectOperationStage::Package,
                   0.72F + taskFraction * 0.23F,
                   "Gradle " + current + " (" +
                       std::to_string(completed) + "/" +
                       std::to_string(total) + ")");
          }
        }
      }
    } catch (const nlohmann::json::exception &) {
      // Gradle replaces the progress file atomically. A transient partial
      // read is retried on the next poll without changing visible progress.
    }

    usleep(50'000);
  }
  if (completedFromMarker)
    return true;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    add(issues, "BUILD_ANDROID_GRADLE_FAILED",
        "Gradle did not produce a successful Android " +
            variant.configuration + " package.",
        androidRoot);
    return false;
  }
  return true;
}
#endif

} // namespace

ProjectOperationResult
runProjectOperation(const ProjectOperationRequest &request) {
  ProjectOperationResult result;
  report(request, ProjectOperationStage::Validate, 0.05F, "Validating project");
  const ValidationSummary validation = validatePath(request.projectFile);
  result.diagnostics = validation.diagnostics;
  if (hasErrors(result.diagnostics)) {
    result.stage = ProjectOperationStage::Failed;
    return result;
  }
  if (cancelled(request)) {
    result.stage = ProjectOperationStage::Cancelled;
    return result;
  }
  if (request.operation == ProjectOperation::Validate) {
    report(request, ProjectOperationStage::Complete, 1.0F,
           "Validation complete");
    result.stage = ProjectOperationStage::Complete;
    result.artifact = request.projectFile;
    return result;
  }

  if (request.operation == ProjectOperation::CookLinux) {
    const auto output =
        request.outputDirectory.empty()
            ? request.projectFile.parent_path() / "generated/cooked/linux"
            : request.outputDirectory;
    report(request, ProjectOperationStage::Cook, 0.20F,
           "Cooking Linux content");
    if (!cookTransactionally(request, output, "linux", result.diagnostics)) {
      result.stage = cancelled(request) ? ProjectOperationStage::Cancelled
                                        : ProjectOperationStage::Failed;
      return result;
    }
    result.stage = ProjectOperationStage::Complete;
    result.artifact = output;
    report(request, ProjectOperationStage::Complete, 1.0F, "Cook complete");
    return result;
  }

  if (request.operation == ProjectOperation::PackageLinux ||
      request.operation == ProjectOperation::PackageLinuxServer) {
    const bool server =
        request.operation == ProjectOperation::PackageLinuxServer;
    const auto output =
        request.outputDirectory.empty()
            ? request.projectFile.parent_path() /
                  (server ? "build/linux_server" : "build/linux") /
                  request.projectFile.parent_path().filename()
            : request.outputDirectory;
    const auto staging = uniqueSibling(output, "staging");
    TemporaryDirectory cleanup{staging};
    report(request, ProjectOperationStage::Cook, 0.20F,
           "Cooking Linux package content");
    const Diagnostics cooked =
        assets::cookProject({.projectFile = request.projectFile,
                             .outputDirectory = staging / "project",
                             .platform = server ? "linux_server" : "linux",
                             .shaderCompiler = {},
                             .shaderIncludeDirectory = {}});
    result.diagnostics.insert(result.diagnostics.end(), cooked.begin(),
                              cooked.end());
    if (hasErrors(result.diagnostics) || cancelled(request)) {
      result.stage = cancelled(request) ? ProjectOperationStage::Cancelled
                                        : ProjectOperationStage::Failed;
      return result;
    }
    report(request, ProjectOperationStage::Package, 0.80F,
           "Writing Linux package");
    const std::filesystem::path runtime =
        server ? request.runtimeExecutable.parent_path() / "demi-server"
               : request.runtimeExecutable;
    const Diagnostics staged = stageLinuxPackage(
        {.projectFile = request.projectFile,
         .runtimeExecutable = runtime,
         .stagingDirectory = staging,
         .server = server});
    result.diagnostics.insert(result.diagnostics.end(), staged.begin(),
                              staged.end());
    if (hasErrors(result.diagnostics)) {
      result.stage = ProjectOperationStage::Failed;
      return result;
    }
    if (cancelled(request)) {
      result.stage = ProjectOperationStage::Cancelled;
      return result;
    }
    if (!commitDirectory(staging, output, result.diagnostics)) {
      result.stage = ProjectOperationStage::Failed;
      return result;
    }
    cleanup.path.clear();
    result.stage = ProjectOperationStage::Complete;
    result.artifact = output;
    report(request, ProjectOperationStage::Complete, 1.0F,
           "Linux package complete");
    return result;
  }

  const auto androidVariant = androidPackageVariant(request.operation);
  if (!androidVariant) {
    add(result.diagnostics, "BUILD_OPERATION_UNSUPPORTED",
        "The requested project build operation is unsupported.");
    result.stage = ProjectOperationStage::Failed;
    return result;
  }
  if (!validateAndroidSigningEnvironment(*androidVariant,
                                         result.diagnostics)) {
    result.stage = ProjectOperationStage::Failed;
    return result;
  }
  const auto buildSettings = loadProjectBuildSettings(request.projectFile);
  if (!buildSettings) {
    add(result.diagnostics, "BUILD_ANDROID_SETTINGS_READ_FAILED",
        "Could not read validated Android project settings.",
        request.projectFile);
    result.stage = ProjectOperationStage::Failed;
    return result;
  }

  const auto androidRoot = request.engineRoot / "android";
  const auto cookedProject =
      androidRoot / "app/build/generated/demi/cooked-project";
  report(request, ProjectOperationStage::Cook, 0.20F,
         "Cooking Android content");
  if (!cookTransactionally(request, cookedProject, "android",
                           result.diagnostics)) {
    result.stage = cancelled(request) ? ProjectOperationStage::Cancelled
                                      : ProjectOperationStage::Failed;
    return result;
  }
  report(request, ProjectOperationStage::Package, 0.72F,
         "Running Android packaging");
#if defined(__linux__)
  if (!runAndroidGradle(request, cookedProject / request.projectFile.filename(),
                        *androidVariant, result.diagnostics)) {
    result.stage = cancelled(request) ? ProjectOperationStage::Cancelled
                                      : ProjectOperationStage::Failed;
    return result;
  }
  report(request, ProjectOperationStage::Package, 0.96F,
         "Finalizing Android package");
#else
  add(result.diagnostics, "BUILD_ANDROID_UNSUPPORTED",
      "Android packaging is only available from a Linux host.", androidRoot);
  result.stage = ProjectOperationStage::Failed;
  return result;
#endif
  const auto gradleArtifact = androidRoot / androidVariant->gradleArtifact;
  if (!std::filesystem::is_regular_file(gradleArtifact)) {
    add(result.diagnostics, "BUILD_ANDROID_ARTIFACT_MISSING",
        "Gradle completed but the Android artifact was not found.",
        gradleArtifact);
    result.stage = ProjectOperationStage::Failed;
    return result;
  }
  const std::filesystem::path outputDirectory =
      request.outputDirectory.empty()
          ? request.projectFile.parent_path() / "build/android"
          : request.outputDirectory;
  result.artifact = outputDirectory /
                    (buildSettings->executableName +
                     androidVariant->outputSuffix);
  report(request, ProjectOperationStage::Package, 0.98F,
         "Publishing Android package");
  if (!publishFile(gradleArtifact, result.artifact, result.diagnostics)) {
    result.stage = ProjectOperationStage::Failed;
    return result;
  }
  if (!writeAndroidPackageReport(
          request, *androidVariant, *buildSettings,
          cookedProject / request.projectFile.filename(), result.artifact,
          result.diagnostics)) {
    result.stage = ProjectOperationStage::Failed;
    return result;
  }
  result.stage = ProjectOperationStage::Complete;
  report(request, ProjectOperationStage::Complete, 1.0F,
         "Android package complete");
  return result;
}

std::string_view projectOperationStageName(const ProjectOperationStage stage) {
  switch (stage) {
  case ProjectOperationStage::Validate:
    return "validate";
  case ProjectOperationStage::Cook:
    return "cook";
  case ProjectOperationStage::Package:
    return "package";
  case ProjectOperationStage::Complete:
    return "complete";
  case ProjectOperationStage::Cancelled:
    return "cancelled";
  case ProjectOperationStage::Failed:
    return "failed";
  }
  return "failed";
}

} // namespace demi::build
