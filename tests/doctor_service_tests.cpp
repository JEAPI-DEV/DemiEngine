#include "cli/doctor/DoctorService.h"
#include "demi/capabilities/PlatformCapabilities.h"

#include <cassert>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace {

bool hasCode(const demi::Diagnostics &diagnostics, const std::string &code) {
  for (const auto &diagnostic : diagnostics)
    if (diagnostic.code == code)
      return true;
  return false;
}

} // namespace

int main() {
  using namespace demi::cli::doctor;
  const std::filesystem::path project =
      std::filesystem::path(DEMI_SOURCE_DIR) /
      "examples/production_2d_foundation/demi.project.json";
  std::unordered_map<std::string, std::string> variables;
  DoctorEnvironment environment{
      .commandAvailable =
          [](const std::string &command) {
            return command == "cmake" || command == "ninja" || command == "c++";
          },
      .variable = [&](const std::string &name) -> std::optional<std::string> {
        const auto found = variables.find(name);
        return found == variables.end() ? std::nullopt
                                        : std::optional(found->second);
      },
      .directoryWritable = [](const std::filesystem::path &) { return true; }};

  DoctorService doctor(environment);
  auto diagnostics = doctor.inspect({.projectPath = project});
  assert(!demi::hasErrors(diagnostics));
  assert(hasCode(diagnostics, "DOCTOR_PROJECT_VALID"));
  assert(hasCode(diagnostics, "DOCTOR_GRAPHICS_API_VALID"));
  assert(hasCode(diagnostics, "DOCTOR_USER_DATA_UNRESOLVED"));
  assert(hasCode(diagnostics, "DOCTOR_CACHE_UNRESOLVED"));

  variables["DEMI_GRAPHICS_API"] = "directx-eleventy";
  diagnostics = doctor.inspect({.projectPath = project});
  assert(hasCode(diagnostics, "DOCTOR_GRAPHICS_API_INVALID"));

  variables.clear();
  diagnostics = doctor.inspect({.projectPath = project, .platform = "android"});
  assert(hasCode(diagnostics, "DOCTOR_ANDROID_SDK_MISSING"));
  assert(hasCode(diagnostics, "DOCTOR_ANDROID_NDK_MISSING"));
  assert(hasCode(diagnostics, "DOCTOR_JAVA_MISSING"));

  diagnostics = doctor.inspect({.projectPath = project, .platform = "console"});
  assert(hasCode(diagnostics, "DOCTOR_PLATFORM_UNKNOWN"));
  diagnostics = doctor.inspect({.projectPath = "does-not-exist"});
  assert(hasCode(diagnostics, "DOCTOR_PROJECT_NOT_FOUND"));

  // Optional runtime feature support is reported against the requested
  // platform. The reference project uses no optional feature, so a runtime
  // configured without media, networking, or SVG only warns.
  const demi::capabilities::RuntimeFeatures bareHost{
      .graphicsRuntime = true, .network = false, .media = false, .svg = false};
  diagnostics = doctor.inspect(
      {.projectPath = project, .platform = "linux", .hostFeatures = bareHost});
  assert(hasCode(diagnostics, "DOCTOR_OPTIONAL_FEATURE_MISSING"));
  assert(!demi::hasErrors(diagnostics));

  // The Android runtime never includes FFmpeg media or librsvg in v1, and
  // the doctor reports them without failing a valid project.
  diagnostics = doctor.inspect(
      {.projectPath = project,
       .platform = "android",
       .hostFeatures = demi::capabilities::fullyConfiguredRuntimeFeatures()});
  assert(hasCode(diagnostics, "DOCTOR_OPTIONAL_FEATURE_MISSING"));
  for (const auto &diagnostic : diagnostics) {
    const bool capabilityError =
        diagnostic.severity == demi::Severity::Error &&
        diagnostic.code.rfind("PROJECT_BUILD_FEATURE_", 0) == 0;
    assert(!capabilityError);
  }
}
