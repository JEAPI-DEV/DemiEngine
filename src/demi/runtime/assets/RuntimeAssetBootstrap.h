#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace demi::assets {
struct AssetGroupProgress;
}
namespace demi::runtime {
struct World;
using AssetBootstrapPump =
    std::function<bool(const assets::AssetGroupProgress &)>;

class RuntimeAssetService;

struct SceneAssetBootstrapResult {
  bool success = false;
  bool hasResidentGroup = false;
};

// Used only before the frame loop, after native backends exist. Runtime scene
// transitions remain asynchronous through SceneFlow.
[[nodiscard]] SceneAssetBootstrapResult
prepareInitialSceneAssets(RuntimeAssetService &assets, std::string_view sceneId,
                          std::span<const std::string> preloadedAssets = {},
                          Diagnostics *diagnostics = nullptr,
                          const World *preparedWorld = nullptr,
                          const AssetBootstrapPump &pump = {});

} // namespace demi::runtime
