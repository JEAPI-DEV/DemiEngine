#pragma once

#include "demi/capabilities/PlatformCapabilities.h"
#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace demi::build {

enum class ProjectOperation {
  Validate,
  CookLinux,
  PackageLinux,
  PackageLinuxServer,
  PackageAndroid,
  PackageAndroidRelease,
  BundleAndroidRelease
};

enum class ProjectOperationStage {
  Validate,
  Cook,
  Package,
  Complete,
  Cancelled,
  Failed
};

struct ProjectOperationProgress {
  ProjectOperationStage stage = ProjectOperationStage::Validate;
  float fraction = 0.0F;
  std::string message;
};

struct ProjectOperationRequest {
  ProjectOperation operation = ProjectOperation::Validate;
  std::filesystem::path projectFile;
  std::filesystem::path outputDirectory;
  std::filesystem::path engineRoot;
  std::filesystem::path runtimeExecutable;
  std::string gradleExecutable = "gradle";
  // Optional engine features of the host renderer runtime; used for
  // build-time platform capability checks. When absent, the fully
  // configured default toolchain is assumed.
  std::optional<capabilities::RuntimeFeatures> hostFeatures;
  std::function<bool()> isCancelled;
  std::function<void(const ProjectOperationProgress &)> reportProgress;
};

struct ProjectOperationResult {
  ProjectOperationStage stage = ProjectOperationStage::Failed;
  Diagnostics diagnostics;
  std::filesystem::path artifact;

  [[nodiscard]] bool succeeded() const {
    return stage == ProjectOperationStage::Complete && !hasErrors(diagnostics);
  }
};

// Synchronous structured service shared by CLI and editor adapters. Callers
// own threading; cancellation is observed between deterministic phases and by
// the Android child process.
[[nodiscard]] ProjectOperationResult
runProjectOperation(const ProjectOperationRequest &request);

[[nodiscard]] std::string_view
projectOperationStageName(ProjectOperationStage stage);

} // namespace demi::build
