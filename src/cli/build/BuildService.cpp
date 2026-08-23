#include "cli/build/BuildService.h"

#include "demi/assets/AssetCooker.h"
#include "demi/schema/Validation.h"

#include <chrono>
#include <fstream>
#include <system_error>

#if defined(__linux__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
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

bool copyRuntime(const std::filesystem::path &runtime,
                 const std::filesystem::path &output, Diagnostics &issues) {
  if (!std::filesystem::is_regular_file(runtime)) {
    add(issues, "BUILD_RUNTIME_NOT_FOUND",
        "The Demi runtime executable was not found.", runtime);
    return false;
  }
  std::error_code error;
  const auto target = output / "bin/demi";
  std::filesystem::create_directories(target.parent_path(), error);
  if (!error)
    std::filesystem::copy_file(
        runtime, target, std::filesystem::copy_options::overwrite_existing,
        error);
  if (error) {
    add(issues, "BUILD_RUNTIME_COPY_FAILED", error.message(), target);
    return false;
  }
  std::filesystem::permissions(target,
                               std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec,
                               std::filesystem::perm_options::add, error);
  return true;
}

bool writeLauncher(const std::filesystem::path &projectFile,
                   const std::filesystem::path &output, const bool server,
                   Diagnostics &issues) {
  std::string name = projectFile.parent_path().filename().string();
  if (name.empty() || name == "project" || name == "bin")
    name = "demi-game";
  const auto path = output / (server ? "serve" : name);
  std::ofstream launcher(path);
  if (!launcher) {
    add(issues, "BUILD_LAUNCHER_WRITE_FAILED",
        "Could not write the Linux launcher.", path);
    return false;
  }
  launcher << "#!/usr/bin/env sh\n"
              "set -eu\n"
              "DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n"
              "exec \"$DIR/bin/demi\" "
           << (server ? "serve" : "run")
           << " --project \"$DIR/project/demi.project.json\" \"$@\"\n";
  launcher.close();
  std::error_code error;
  std::filesystem::permissions(path,
                               std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec,
                               std::filesystem::perm_options::add, error);
  return true;
}

#if defined(__linux__)
bool runAndroidGradle(const ProjectOperationRequest &request,
                      const std::filesystem::path &packagedProject,
                      Diagnostics &issues) {
  const auto androidRoot = request.engineRoot / "android";
  const std::string projectProperty =
      "-PdemiProjectFile=" + packagedProject.string();
  const pid_t child = fork();
  if (child < 0) {
    add(issues, "BUILD_ANDROID_PROCESS_FAILED",
        "Could not start the Android build process.", androidRoot);
    return false;
  }
  if (child == 0) {
    (void)setpgid(0, 0);
    execlp(request.gradleExecutable.c_str(), request.gradleExecutable.c_str(),
           "--no-daemon", "-p", androidRoot.c_str(), projectProperty.c_str(),
           ":app:assembleDebug", static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = 0;
  (void)setpgid(child, child);
  while (waitpid(child, &status, WNOHANG) == 0) {
    if (cancelled(request)) {
      (void)kill(-child, SIGTERM);
      (void)waitpid(child, &status, 0);
      return false;
    }
    usleep(50'000);
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    add(issues, "BUILD_ANDROID_GRADLE_FAILED",
        "Gradle did not produce a successful Android debug package.",
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
    if (!copyRuntime(runtime, staging, result.diagnostics) ||
        !writeLauncher(request.projectFile, staging, server,
                       result.diagnostics)) {
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
                        result.diagnostics)) {
    result.stage = cancelled(request) ? ProjectOperationStage::Cancelled
                                      : ProjectOperationStage::Failed;
    return result;
  }
#else
  add(result.diagnostics, "BUILD_ANDROID_UNSUPPORTED",
      "Android packaging is only available from a Linux host.", androidRoot);
  result.stage = ProjectOperationStage::Failed;
  return result;
#endif
  result.artifact = androidRoot / "app/build/outputs/apk/debug/app-debug.apk";
  if (!std::filesystem::is_regular_file(result.artifact)) {
    add(result.diagnostics, "BUILD_ANDROID_ARTIFACT_MISSING",
        "Gradle completed but the debug APK was not found.", result.artifact);
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
