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

} // namespace

SceneAssetBootstrapResult
prepareInitialSceneAssets(RuntimeAssetService &service,
                          const std::string_view sceneId,
                          Diagnostics *diagnostics) {
  Diagnostics prepareDiagnostics;
  const auto request = service.prepareScene(sceneId, &prepareDiagnostics);
  if (hasErrors(prepareDiagnostics)) {
    if (diagnostics != nullptr)
      diagnostics->insert(diagnostics->end(), prepareDiagnostics.begin(),
                          prepareDiagnostics.end());
    return {};
  }
  if (request == 0)
    return {.success = true, .hasResidentGroup = false};

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < deadline) {
    service.update();
    const auto progress = service.progress(request);
    if (progress.stage == assets::AssetGroupStage::Ready) {
      if (!service.activate(request, diagnostics))
        return {};
      return {.success = true, .hasResidentGroup = true};
    }
    if (progress.stage == assets::AssetGroupStage::Failed ||
        progress.stage == assets::AssetGroupStage::Cancelled) {
      report(diagnostics, "ASSET_STARTUP_PREPARE_FAILED",
             progress.error.empty() ? "Initial scene assets failed to prepare."
                                    : progress.error,
             std::string(sceneId));
      return {};
    }
    std::this_thread::yield();
  }
  (void)service.cancel(request);
  report(diagnostics, "ASSET_STARTUP_PREPARE_TIMEOUT",
         "Initial scene assets did not become ready within 30 seconds.",
         std::string(sceneId));
  return {};
}

} // namespace demi::runtime
