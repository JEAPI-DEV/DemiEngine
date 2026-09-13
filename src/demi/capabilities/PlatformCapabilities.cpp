#include "demi/capabilities/PlatformCapabilities.h"

namespace demi::capabilities {

std::string_view targetPlatformName(TargetPlatform platform) {
  switch (platform) {
  case TargetPlatform::Linux:
    return "linux";
  case TargetPlatform::LinuxServer:
    return "linux_server";
  case TargetPlatform::Android:
    return "android";
  }
  return "linux";
}

RuntimeFeatures fullyConfiguredRuntimeFeatures() {
  return {.graphicsRuntime = true,
          .network = true,
          .media = true,
          .svg = true};
}

RuntimeFeatures
linuxServerRuntimeFeatures(const RuntimeFeatures &hostRendererRuntime) {
  return {.graphicsRuntime = false,
          .network = hostRendererRuntime.network,
          .media = hostRendererRuntime.media,
          .svg = false};
}

RuntimeFeatures androidRuntimeFeatures() {
  return {.graphicsRuntime = true,
          .network = true,
          .media = false,
          .svg = false};
}

RuntimeFeatures
targetRuntimeFeatures(TargetPlatform platform,
                      const RuntimeFeatures &hostRendererRuntime) {
  switch (platform) {
  case TargetPlatform::Linux:
    return hostRendererRuntime;
  case TargetPlatform::LinuxServer:
    return linuxServerRuntimeFeatures(hostRendererRuntime);
  case TargetPlatform::Android:
    return androidRuntimeFeatures();
  }
  return hostRendererRuntime;
}

} // namespace demi::capabilities
