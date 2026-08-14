#include "demi/runtime/assets/RuntimeAssetReload.h"

#include "demi/runtime/assets/RuntimeAssetService.h"

namespace demi::runtime {

bool applyWatchedAssetRegistry(RuntimeAssetService &service,
                               AssetRegistry &active, AssetRegistry candidate,
                               Diagnostics *diagnostics) {
  AssetRegistry previous = active;
  active = std::move(candidate);
  if (service.reloadChangedResidentAssets(previous, diagnostics))
    return true;

  active = std::move(previous);
  Diagnostics restoreDiagnostics;
  if (!service.restoreResources(&restoreDiagnostics) && diagnostics != nullptr)
    diagnostics->insert(diagnostics->end(), restoreDiagnostics.begin(),
                        restoreDiagnostics.end());
  return false;
}

} // namespace demi::runtime
