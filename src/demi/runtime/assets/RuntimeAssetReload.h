#pragma once

#include "demi/assets/AssetRegistry.h"

namespace demi::runtime {

class RuntimeAssetService;

// Commits a validated watched registry only when every changed resident asset
// can be replaced through its ordinary loader. On failure the previous
// registry and native snapshot are restored.
[[nodiscard]] bool
applyWatchedAssetRegistry(RuntimeAssetService &service, AssetRegistry &active,
                          AssetRegistry candidate,
                          Diagnostics *diagnostics = nullptr);

} // namespace demi::runtime
