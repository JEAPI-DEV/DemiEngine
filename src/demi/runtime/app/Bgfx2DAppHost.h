#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/app/BgfxAppContext.h"
#include "demi/runtime/navigation/NavigationGrid2D.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime::render {
class GpuResources;
class RenderCommands;
} // namespace demi::runtime::render

namespace demi::runtime {

struct Bgfx2DAppHostConfig {
  std::string title;
  int width = 960;
  int height = 540;
  render::GraphicsApi graphicsApi = render::GraphicsApi::Automatic;
  bool vsync = true;
  bool debugGraphics = false;
};

// Owns the platform window and bgfx objects in their required lifetime order.
// RuntimeApp remains responsible for simulation; this class only translates
// platform state and presents a 2D world.
class Bgfx2DAppHost {
public:
  Bgfx2DAppHost();
  ~Bgfx2DAppHost();

  Bgfx2DAppHost(const Bgfx2DAppHost &) = delete;
  Bgfx2DAppHost &operator=(const Bgfx2DAppHost &) = delete;

  [[nodiscard]] bool initialize(const Bgfx2DAppHostConfig &config,
                                const AssetRegistry &assets,
                                std::vector<std::string> &diagnostics,
                                std::string &error);
  [[nodiscard]] bool reloadAssets(const AssetRegistry &assets,
                                  std::vector<std::string> &diagnostics,
                                  std::string &error);
  void shutdown();

  void poll(InputState &input);
  [[nodiscard]] const platform::PlatformFrameState &frameState() const;
  [[nodiscard]] bool setWindowMode(std::string_view mode, std::string &error);
  [[nodiscard]] bool setMouseCaptured(bool captured, std::string &error);
  [[nodiscard]] std::string clipboard() const;
  [[nodiscard]] bool setClipboard(const std::string &text, std::string &error);

  [[nodiscard]] bool renderFrame(const World &world,
                                 const Camera2DComponent &camera,
                                 Vec2 cameraPosition, float deltaSeconds,
                                 const navigation::NavigationGrid2D *navigation,
                                 std::string &error);
  [[nodiscard]] std::string_view rendererName() const;

private:
  BgfxAppContext context_;
  class RendererOwner;
  std::unique_ptr<RendererOwner> renderer_;
};

} // namespace demi::runtime
