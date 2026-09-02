#pragma once

#include <string_view>

namespace demi::capabilities {

// Target platforms a project can be validated, cooked, and packaged for.
enum class TargetPlatform {
  Linux,
  LinuxServer,
  Android,
};

[[nodiscard]] std::string_view targetPlatformName(TargetPlatform platform);

// Optional engine features whose availability depends on how the runtime
// was configured. These mirror the DEMI_HAS_* toolchain decisions made by
// cmake/DemiRuntime.cmake and android/app/build.gradle.
struct RuntimeFeatures {
  bool graphicsRuntime = true;
  bool network = false;
  bool media = false;
  bool svg = false;

  [[nodiscard]] bool operator==(const RuntimeFeatures &) const = default;
};

// Assumed feature profile when a caller does not supply the host build's
// configuration: every optional feature available.
[[nodiscard]] RuntimeFeatures fullyConfiguredRuntimeFeatures();

// Features of the dedicated-server runtime derived from the host renderer
// runtime configuration. Both libraries are configured by the same CMake
// options except that the server runtime never compiles a renderer, so its
// graphics and runtime SVG support are always absent.
[[nodiscard]] RuntimeFeatures
linuxServerRuntimeFeatures(const RuntimeFeatures &hostRendererRuntime);

// Features of the Android runtime produced by the engine-owned Gradle
// packaging. The Gradle configuration compiles the engine without FFmpeg
// media and without librsvg; networking and the graphics runtime are always
// present.
[[nodiscard]] RuntimeFeatures androidRuntimeFeatures();

// Resolves the runtime features of a packaged target. The Linux target uses
// the features of the renderer runtime binary being packaged.
[[nodiscard]] RuntimeFeatures
targetRuntimeFeatures(TargetPlatform platform,
                      const RuntimeFeatures &hostRendererRuntime);

} // namespace demi::capabilities
