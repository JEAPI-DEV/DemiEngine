#pragma once

#include "demi/runtime/platform/PlatformHost.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"

#include <memory>
#include <string>
#include <string_view>

namespace demi::runtime::render {
class GpuResources;
class RenderCommands;
} // namespace demi::runtime::render

namespace demi::runtime {

struct BgfxAppContextConfig {
  std::string title;
  int width = 960;
  int height = 540;
  render::GraphicsApi graphicsApi = render::GraphicsApi::Automatic;
  bool vsync = true;
  bool debugGraphics = false;
};

// Shared SDL/bgfx composition root. Visible renderers own only their renderer
// resources; this context owns the platform and graphics lifecycles once.
class BgfxAppContext {
public:
  BgfxAppContext();
  ~BgfxAppContext();

  BgfxAppContext(const BgfxAppContext &) = delete;
  BgfxAppContext &operator=(const BgfxAppContext &) = delete;

  [[nodiscard]] bool initialize(const BgfxAppContextConfig &config,
                                std::string &error);
  void shutdown();
  [[nodiscard]] bool beginFrame(std::string &error);
  void endFrame();

  void poll(InputState &input);
  [[nodiscard]] const platform::PlatformFrameState &frameState() const;
  [[nodiscard]] bool setWindowMode(std::string_view mode, std::string &error);
  [[nodiscard]] bool setMouseCaptured(bool captured, std::string &error);
  [[nodiscard]] std::string clipboard() const;
  [[nodiscard]] bool setClipboard(const std::string &text, std::string &error);
  [[nodiscard]] bool requestPermission(
      const std::string &permission,
      std::function<void(bool granted, bool deniedPermanently)> result,
      std::string &error);
  [[nodiscard]] std::string_view rendererName() const;
  [[nodiscard]] render::GpuResources *resources() const;
  [[nodiscard]] render::RenderCommands *commands() const;
  [[nodiscard]] std::uint16_t viewportWidth() const;
  [[nodiscard]] std::uint16_t viewportHeight() const;
  [[nodiscard]] bool initialized() const { return initialized_; }

private:
  std::unique_ptr<platform::PlatformHost> platform_;
  render::BgfxGraphicsDevice graphics_;
  std::unique_ptr<render::GpuResources> resources_;
  std::unique_ptr<render::RenderCommands> commands_;
  int renderWidth_ = 0;
  int renderHeight_ = 0;
  unsigned surfaceGeneration_ = 0;
  bool frameOpen_ = false;
  bool initialized_ = false;
};

[[nodiscard]] render::GraphicsApi configuredGraphicsApi();

} // namespace demi::runtime
