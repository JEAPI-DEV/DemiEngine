#include "demi/runtime/assets/RuntimeAssetBootstrap.h"

#include "demi/runtime/assets/RuntimeAssetService.h"

#include <chrono>
#include <thread>

namespace demi::runtime {
namespace {

void report(Diagnostics *diagnostics, std::string code, std::string message,
            std::string path) {
  if (diagnostics != nullptr)
    diagnostics->push_back({.severity = Severity::Error,
                            .code = std::move(code),
                            .message = std::move(message),
                            .path = std::move(path),
                            .suggestion = {}});
}

bool prepareAndActivate(RuntimeAssetService &service,
                        const assets::AssetGroupRequestHandle request,
                        const std::string_view owner, Diagnostics *diagnostics,
                        const AssetBootstrapPump &pump) {
  if (request == 0)
    return false;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pump && !pump(service.progress(request))) {
      (void)service.cancel(request);
      report(diagnostics, "ASSET_STARTUP_CANCELLED",
             "Startup loading was cancelled.", std::string(owner));
      return false;
    }
    service.update();
    const auto progress = service.progress(request);
    if (progress.stage == assets::AssetGroupStage::Ready)
      return service.activate(request, diagnostics);
    if (progress.stage == assets::AssetGroupStage::Failed ||
        progress.stage == assets::AssetGroupStage::Cancelled) {
      report(diagnostics, "ASSET_STARTUP_PREPARE_FAILED",
             progress.error.empty() ? "Startup assets failed to prepare."
                                    : progress.error,
             std::string(owner));
      return false;
    }
    std::this_thread::yield();
  }
  const auto progress = service.progress(request);
  (void)service.cancel(request);
  report(diagnostics, "ASSET_STARTUP_PREPARE_TIMEOUT",
         "Startup assets did not become ready within 30 seconds (stage: " +
             std::string(assets::assetGroupStageName(progress.stage)) + ", " +
             std::to_string(progress.completedAssets) + "/" +
             std::to_string(progress.totalAssets) + " assets complete).",
         std::string(owner));
  return false;
}

} // namespace

SceneAssetBootstrapResult
prepareInitialSceneAssets(RuntimeAssetService &service,
                          const std::string_view sceneId,
                          const std::span<const std::string> preloadedAssets,
                          Diagnostics *diagnostics, const World *preparedWorld,
                          const AssetBootstrapPump &pump) {
  for (const std::string &uri : preloadedAssets) {
    Diagnostics prepareDiagnostics;
    const auto request = service.prepare(uri, &prepareDiagnostics);
    if (hasErrors(prepareDiagnostics)) {
      if (diagnostics != nullptr)
        diagnostics->insert(diagnostics->end(), prepareDiagnostics.begin(),
                            prepareDiagnostics.end());
      return {};
    }
    if (!prepareAndActivate(service, request, uri, diagnostics, pump))
      return {};
  }

  Diagnostics prepareDiagnostics;
  const auto request =
      preparedWorld
          ? service.prepareScene(sceneId, *preparedWorld, &prepareDiagnostics)
          : service.prepareScene(sceneId, &prepareDiagnostics);
  if (hasErrors(prepareDiagnostics)) {
    if (diagnostics != nullptr)
      diagnostics->insert(diagnostics->end(), prepareDiagnostics.begin(),
                          prepareDiagnostics.end());
    return {};
  }
  if (request == 0)
    return {.success = true, .hasResidentGroup = false};
  return {.success =
              prepareAndActivate(service, request, sceneId, diagnostics, pump),
          .hasResidentGroup = true};
}

} // namespace demi::runtime
