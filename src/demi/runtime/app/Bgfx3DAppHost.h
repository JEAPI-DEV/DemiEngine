#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/app/BgfxAppContext.h"
#include "demi/runtime/camera/CameraRenderScheduler3D.h"
#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/render/RenderStatistics.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace demi::assets {
class AssetResourceLoader;
}

namespace demi::runtime {

using Bgfx3DAppHostConfig = BgfxAppContextConfig;

// Owns the platform/device/renderer lifecycle for visible 3D worlds.
class Bgfx3DAppHost {
public:
  Bgfx3DAppHost();
  ~Bgfx3DAppHost();

  Bgfx3DAppHost(const Bgfx3DAppHost &) = delete;
  Bgfx3DAppHost &operator=(const Bgfx3DAppHost &) = delete;

  [[nodiscard]] bool initialize(const Bgfx3DAppHostConfig &config,
                                const AssetRegistry &assets,
                                std::vector<std::string> &diagnostics,
                                std::string &error);
  [[nodiscard]] bool reloadAssets(const AssetRegistry &assets,
                                  std::vector<std::string> &diagnostics,
                                  std::string &error);
  [[nodiscard]] std::shared_ptr<assets::AssetResourceLoader>
  createAssetLoader(const AssetRegistry &source);
  void shutdown();
  void poll(InputState &input);
  [[nodiscard]] const platform::PlatformFrameState &frameState() const;
  [[nodiscard]] bool setWindowMode(std::string_view mode, std::string &error);
  [[nodiscard]] bool setMouseCaptured(bool captured, std::string &error);
  [[nodiscard]] bool requestFrameRate(float framesPerSecond);
  [[nodiscard]] std::string clipboard() const;
  [[nodiscard]] bool setClipboard(const std::string &text, std::string &error);
  [[nodiscard]] bool requestPermission(
      const std::string &permission,
      std::function<void(bool granted, bool deniedPermanently)> result,
      std::string &error);
  [[nodiscard]] bool renderFrame(const World &world,
                                 const render::BgfxCameraFrame3D &camera,
                                 float deltaSeconds, std::string &error);
  [[nodiscard]] bool
  renderFrames(const World &world,
               std::span<const render::BgfxCameraFrame3D> cameras,
               float deltaSeconds, std::string &error);
  [[nodiscard]] std::string_view rendererName() const;
  [[nodiscard]] const RenderStatistics &statistics() const;
  [[nodiscard]] double lastExtractionMilliseconds() const;

private:
  class RendererOwner;
  BgfxAppContext context_;
  std::unique_ptr<RendererOwner> renderer_;
  CameraRenderScheduler3D cameraScheduler_;
  RenderStatistics frameStatistics_;
  double frameExtractionMilliseconds_ = 0.0;
};

} // namespace demi::runtime
