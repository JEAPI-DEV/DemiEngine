#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace demi {
struct AssetRegistry;
namespace assets {
class AssetResourceLoader;
}
namespace runtime {
struct World;

// Optional visible-host services. The embedded session still owns the sole
// runtime world, scene flow and physics; the host owns GPU resources only.
// Callbacks borrow the host, which must outlive the session through stop().
struct EmbeddedRuntimeHostServices {
  std::function<std::shared_ptr<assets::AssetResourceLoader>(
      const AssetRegistry &, std::string &error)>
      createAssetLoader;
  std::function<bool(const World &, std::uint16_t viewportWidth,
                     std::uint16_t viewportHeight, bool &complete,
                     std::string &error)>
      prepareScenePresentation;
  std::function<void()> cancelScenePresentation;
};

} // namespace runtime
} // namespace demi
