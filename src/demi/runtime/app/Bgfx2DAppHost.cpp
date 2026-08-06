#include "demi/runtime/app/Bgfx2DAppHost.h"

#include "demi/runtime/render/BgfxRenderer2D.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/RenderCommands.h"

namespace demi::runtime {

class Bgfx2DAppHost::RendererOwner {
public:
  RendererOwner(render::GpuResources &resources,
                render::RenderCommands &commands)
      : renderer(resources, commands) {}

  render::BgfxRenderer2D renderer;
};

Bgfx2DAppHost::Bgfx2DAppHost() = default;

Bgfx2DAppHost::~Bgfx2DAppHost() { shutdown(); }

bool Bgfx2DAppHost::initialize(const Bgfx2DAppHostConfig &config,
                               const AssetRegistry &assets,
                               std::vector<std::string> &diagnostics,
                               std::string &error) {
  if (renderer_ != nullptr) {
    error = "The bgfx 2D application host is already initialized.";
    return false;
  }
  if (!context_.initialize({.title = config.title,
                            .width = config.width,
                            .height = config.height,
                            .graphicsApi = config.graphicsApi,
                            .vsync = config.vsync,
                            .debugGraphics = config.debugGraphics},
                           error))
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

bool Bgfx2DAppHost::reloadAssets(const AssetRegistry &assets,
                                 std::vector<std::string> &diagnostics,
                                 std::string &error) {
  if (renderer_ == nullptr) {
    error = "The bgfx 2D application host is not initialized.";
    return false;
  }
  auto candidate = std::make_unique<RendererOwner>(*context_.resources(),
                                                   *context_.commands());
  if (!candidate->renderer.initialize(error))
    return false;
  if (!candidate->renderer.loadAssets(assets, diagnostics)) {
    candidate->renderer.shutdown();
    error = "One or more 2D assets failed to load.";
    return false;
  }
  renderer_->renderer.shutdown();
  renderer_ = std::move(candidate);
  return true;
}

void Bgfx2DAppHost::shutdown() {
  if (renderer_ != nullptr)
    renderer_->renderer.shutdown();
  renderer_.reset();
  context_.shutdown();
}

void Bgfx2DAppHost::poll(InputState &input) { context_.poll(input); }

const platform::PlatformFrameState &Bgfx2DAppHost::frameState() const {
  return context_.frameState();
}

bool Bgfx2DAppHost::setWindowMode(const std::string_view mode,
                                  std::string &error) {
  return context_.setWindowMode(mode, error);
}

bool Bgfx2DAppHost::setMouseCaptured(const bool captured, std::string &error) {
  return context_.setMouseCaptured(captured, error);
}

std::string Bgfx2DAppHost::clipboard() const { return context_.clipboard(); }

bool Bgfx2DAppHost::setClipboard(const std::string &text, std::string &error) {
  return context_.setClipboard(text, error);
}

bool Bgfx2DAppHost::renderFrame(const World &world,
                                const Camera2DComponent &camera,
                                const Vec2 cameraPosition,
                                const float deltaSeconds,
                                const navigation::NavigationGrid2D *navigation,
                                std::string &error) {
  if (renderer_ == nullptr) {
    error = "The bgfx 2D application host is not initialized.";
    return false;
  }
  if (!context_.beginFrame(error))
    return false;
  const bool began = renderer_->renderer.beginFrame(
      camera, cameraPosition, context_.viewportWidth(),
      context_.viewportHeight(), deltaSeconds, error);
  if (!began) {
    context_.endFrame();
    return false;
  }

  bool rendered = renderer_->renderer.drawWorld(world);
  if (rendered && navigation != nullptr)
    rendered = renderer_->renderer.drawNavigation(*navigation);
  if (rendered)
    rendered = renderer_->renderer.drawHud(world);
  std::string flushError;
  const bool flushed = renderer_->renderer.endFrame(flushError);
  context_.endFrame();
  if (!rendered) {
    error = "A bgfx 2D draw command could not be queued.";
    return false;
  }
  if (!flushed) {
    error = std::move(flushError);
    return false;
  }
  return true;
}

std::string_view Bgfx2DAppHost::rendererName() const {
  return context_.rendererName();
}

} // namespace demi::runtime
