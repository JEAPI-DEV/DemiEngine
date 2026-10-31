#pragma once
#include <string_view>
#include "demi/runtime/scene/model/World.h"

namespace demi::assets {
struct AssetGroupProgress;
}
namespace demi::runtime {
class Bgfx2DAppHost;
class Bgfx3DAppHost;

// A resource-independent loading view. Pumped on the platform thread while
// startup jobs prepare; it advances graphics work without starting gameplay.
class StartupPresentation {
public:
  explicit StartupPresentation(std::string title);
  // Draw after the caller has polled this frame's platform events.
  bool renderStatus(Bgfx3DAppHost &host, std::string_view status, std::string &error);
  bool pump(Bgfx2DAppHost &host, InputState &input,
            const assets::AssetGroupProgress &progress, std::string &error);
  bool pump(Bgfx3DAppHost &host, InputState &input,
            const assets::AssetGroupProgress &progress, std::string &error);

private:
  void update(const assets::AssetGroupProgress &progress);
  World world_;
};
} // namespace demi::runtime
