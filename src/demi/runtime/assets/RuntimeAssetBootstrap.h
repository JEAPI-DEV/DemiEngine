#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <string_view>

namespace demi::runtime {

class RuntimeAssetService;

struct SceneAssetBootstrapResult {
  bool success = false;
  bool hasResidentGroup = false;
};

// Used only before the frame loop, after native backends exist. Runtime scene
// transitions remain asynchronous through SceneFlow.
[[nodiscard]] SceneAssetBootstrapResult
prepareInitialSceneAssets(RuntimeAssetService &assets, std::string_view sceneId,
                          Diagnostics *diagnostics = nullptr);

} // namespace demi::runtime
