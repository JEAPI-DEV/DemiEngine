#include "demi/runtime/app/Bgfx3DAppHost.h"

#include "demi/runtime/assets/RegistryAssetResourceLoader.h"

#include <utility>

namespace demi::runtime {

class Bgfx3DAppHost::RendererOwner {
public:
  RendererOwner(render::GpuResources &resources,
                render::RenderCommands &commands)
      : renderer(resources, commands) {}

  render::BgfxRenderer3D renderer;
};

Bgfx3DAppHost::Bgfx3DAppHost() = default;

Bgfx3DAppHost::~Bgfx3DAppHost() { shutdown(); }

bool Bgfx3DAppHost::initialize(const Bgfx3DAppHostConfig &config,
                               const AssetRegistry &assets,
                               std::vector<std::string> &diagnostics,
                               std::string &error) {
  if (renderer_ != nullptr) {
    error = "The bgfx 3D application host is already initialized.";
    return false;
  }
  if (!context_.initialize(config, error))
    return false;
  renderer_ = std::make_unique<RendererOwner>(*context_.resources(),
                                              *context_.commands());
  if (!renderer_->renderer.initialize(error)) {
    shutdown();
    return false;
  }
  static_cast<void>(renderer_->renderer.loadAssets(assets, diagnostics));
  return true;
}

bool Bgfx3DAppHost::reloadAssets(const AssetRegistry &assets,
                                 std::vector<std::string> &diagnostics,
                                 std::string &error) {
  if (renderer_ == nullptr) {
    error = "The bgfx 3D application host is not initialized.";
    return false;
  }
  auto candidate = std::make_unique<RendererOwner>(*context_.resources(),
                                                   *context_.commands());
  if (!candidate->renderer.initialize(error))
    return false;
  if (!candidate->renderer.loadAssets(assets, diagnostics)) {
    candidate->renderer.shutdown();
    error = "One or more 3D assets failed to load.";
    return false;
  }
  renderer_->renderer.shutdown();
  renderer_ = std::move(candidate);
  cameraScheduler_ = {};
  return true;
}

std::shared_ptr<assets::AssetResourceLoader>
Bgfx3DAppHost::createAssetLoader(const AssetRegistry &source) {
  return std::make_shared<RegistryAssetResourceLoader>(
      source,
      RegistryAssetBackend{
          .name = "bgfx-3d",
          .assetTypes = {"Font2D", "GifAnimation2D", "Icon2D",
                         "ImageAnimation2D", "Material", "Model3D",
                         "RenderTarget", "Shader", "SvgTexture2D", "Texture2D",
                         "Tilemap2D"},
          .apply = [this](const AssetRegistry &assets, std::string &error) {
            std::vector<std::string> diagnostics;
            if (reloadAssets(assets, diagnostics, error))
              return true;
            if (error.empty() && !diagnostics.empty())
              error = diagnostics.front();
            return false;
          }});
}

void Bgfx3DAppHost::shutdown() {
  if (renderer_ != nullptr)
    renderer_->renderer.shutdown();
  renderer_.reset();
  context_.shutdown();
}

void Bgfx3DAppHost::poll(InputState &input) { context_.poll(input); }

const platform::PlatformFrameState &Bgfx3DAppHost::frameState() const {
  return context_.frameState();
}

bool Bgfx3DAppHost::setWindowMode(const std::string_view mode,
                                  std::string &error) {
  return context_.setWindowMode(mode, error);
}

bool Bgfx3DAppHost::setMouseCaptured(const bool captured, std::string &error) {
  return context_.setMouseCaptured(captured, error);
}

bool Bgfx3DAppHost::requestFrameRate(const float framesPerSecond) {
  return context_.requestFrameRate(framesPerSecond);
}

std::string Bgfx3DAppHost::clipboard() const { return context_.clipboard(); }

bool Bgfx3DAppHost::setClipboard(const std::string &text, std::string &error) {
  return context_.setClipboard(text, error);
}

bool Bgfx3DAppHost::requestPermission(const std::string &permission,
                                      std::function<void(bool, bool)> result,
                                      std::string &error) {
  return context_.requestPermission(permission, std::move(result), error);
}

bool Bgfx3DAppHost::renderFrame(const World &world,
                                const render::BgfxCameraFrame3D &camera,
                                const float deltaSeconds, std::string &error) {
  return renderFrames(world, std::span(&camera, 1), deltaSeconds, error);
}

bool Bgfx3DAppHost::renderFrames(
    const World &world,
    const std::span<const render::BgfxCameraFrame3D> cameras,
    const float deltaSeconds, std::string &error) {
  if (renderer_ == nullptr) {
    error = "The bgfx 3D application host is not initialized.";
    return false;
  }
  if (!context_.beginFrame(error))
    return false;
  bool rendered = !cameras.empty();
  frameStatistics_.reset();
  frameExtractionMilliseconds_ = 0.0;
  cameraScheduler_.beginFrame();
  for (render::BgfxCameraFrame3D current : cameras) {
    if (current.viewportWidth == 0)
      current.viewportWidth = context_.viewportWidth();
    if (current.viewportHeight == 0)
      current.viewportHeight = context_.viewportHeight();
    const std::string_view scheduleId =
        current.cameraId.empty() ? current.camera.renderTarget
                                 : std::string_view(current.cameraId);
    const bool isScheduledTarget = !current.camera.renderTarget.empty();
    current.updateContent =
        !isScheduledTarget ||
        cameraScheduler_.shouldRender(scheduleId, current.camera.updateInterval,
                                      deltaSeconds);
    if (!renderer_->renderer.renderFrame(world, current, deltaSeconds, error)) {
      rendered = false;
      break;
    }
    const RenderStatistics &cameraStatistics = renderer_->renderer.statistics();
    frameStatistics_.batches += cameraStatistics.batches;
    frameStatistics_.triangles += cameraStatistics.triangles;
    frameStatistics_.particles += cameraStatistics.particles;
    frameStatistics_.lights += cameraStatistics.lights;
    frameStatistics_.shadowPasses += cameraStatistics.shadowPasses;
    frameStatistics_.renderTargetBytes += cameraStatistics.renderTargetBytes;
    frameStatistics_.consideredMeshes += cameraStatistics.consideredMeshes;
    frameStatistics_.visibleMeshes += cameraStatistics.visibleMeshes;
    frameStatistics_.culledMeshes += cameraStatistics.culledMeshes;
    frameExtractionMilliseconds_ +=
        renderer_->renderer.lastExtractionMilliseconds();
  }
  cameraScheduler_.endFrame();
  context_.endFrame();
  if (cameras.empty())
    error = "At least one 3D camera frame is required.";
  return rendered;
}

std::string_view Bgfx3DAppHost::rendererName() const {
  return context_.rendererName();
}

const RenderStatistics &Bgfx3DAppHost::statistics() const {
  static const RenderStatistics empty;
  return renderer_ == nullptr ? empty : frameStatistics_;
}

double Bgfx3DAppHost::lastExtractionMilliseconds() const {
  return renderer_ == nullptr ? 0.0 : frameExtractionMilliseconds_;
}

} // namespace demi::runtime
