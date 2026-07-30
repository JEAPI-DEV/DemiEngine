#include "demi/runtime/app/Bgfx2DAppHost.h"

#include "demi/runtime/render/BgfxRenderer2D.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/RenderCommands.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace demi::runtime {

class Bgfx2DAppHost::RendererOwner {
public:
  RendererOwner(render::GpuResources &resources,
                render::RenderCommands &commands)
      : renderer(resources, commands) {}

  render::BgfxRenderer2D renderer;
};

namespace {

platform::WindowMode windowMode(const std::string_view mode, bool &isValid) {
  isValid = true;
  if (mode == "windowed")
    return platform::WindowMode::Windowed;
  if (mode == "borderless")
    return platform::WindowMode::Borderless;
  if (mode == "fullscreen")
    return platform::WindowMode::Fullscreen;
  isValid = false;
  return platform::WindowMode::Windowed;
}

std::uint16_t viewportDimension(const int value) {
  return static_cast<std::uint16_t>(std::clamp(
      value, 1, static_cast<int>(std::numeric_limits<std::uint16_t>::max())));
}

int drawableDimension(const int value) { return std::max(value, 1); }

} // namespace

render::GraphicsApi configuredGraphicsApi() {
  const char *configured = std::getenv("DEMI_GRAPHICS_API");
  if (configured == nullptr || *configured == '\0')
    return render::GraphicsApi::Automatic;
  render::GraphicsApi api = render::GraphicsApi::Automatic;
  return render::parseGraphicsApi(configured, api)
             ? api
             : render::GraphicsApi::Automatic;
}

Bgfx2DAppHost::Bgfx2DAppHost() : platform_(platform::createSdlPlatformHost()) {}

Bgfx2DAppHost::~Bgfx2DAppHost() { shutdown(); }

bool Bgfx2DAppHost::initialize(const Bgfx2DAppHostConfig &config,
                               const AssetRegistry &assets,
                               std::vector<std::string> &diagnostics,
                               std::string &error) {
  if (renderer_ != nullptr) {
    error = "The bgfx 2D application host is already initialized.";
    return false;
  }
  if (!platform_->initialize(
          platform::PlatformHostConfig{
              .title = config.title,
              .width = config.width,
              .height = config.height,
              .resizable = true,
          },
          error))
    return false;

  const platform::PlatformFrameState &state = platform_->frameState();
  const int drawableWidth = drawableDimension(state.width);
  const int drawableHeight = drawableDimension(state.height);
  if (!graphics_.initialize(
          render::GraphicsDeviceConfig{
              .api = config.graphicsApi,
              .nativeWindow = platform_->nativeWindow(),
              .width = static_cast<std::uint32_t>(drawableWidth),
              .height = static_cast<std::uint32_t>(drawableHeight),
              .vsync = config.vsync,
              .debug = config.debugGraphics,
          },
          error)) {
    platform_->shutdown();
    return false;
  }

  resources_ = render::createBgfxGpuResources();
  commands_ = render::createBgfxRenderCommands(*resources_);
  if (commands_ == nullptr) {
    error = "Could not create bgfx render commands.";
    shutdown();
    return false;
  }
  renderer_ = std::make_unique<RendererOwner>(*resources_, *commands_);
  if (!renderer_->renderer.initialize(error)) {
    shutdown();
    return false;
  }
  static_cast<void>(renderer_->renderer.loadAssets(assets, diagnostics));
  renderWidth_ = drawableWidth;
  renderHeight_ = drawableHeight;
  return true;
}

void Bgfx2DAppHost::shutdown() {
  if (renderer_ != nullptr)
    renderer_->renderer.shutdown();
  renderer_.reset();
  commands_.reset();
  if (resources_ != nullptr)
    resources_->clear();
  resources_.reset();
  graphics_.shutdown();
  if (platform_ != nullptr)
    platform_->shutdown();
  renderWidth_ = 0;
  renderHeight_ = 0;
}

void Bgfx2DAppHost::poll(InputState &input) { platform_->poll(input); }

const platform::PlatformFrameState &Bgfx2DAppHost::frameState() const {
  return platform_->frameState();
}

bool Bgfx2DAppHost::setWindowMode(const std::string_view mode,
                                  std::string &error) {
  bool isValid = false;
  const platform::WindowMode parsed = windowMode(mode, isValid);
  if (!isValid) {
    error = "Unknown window mode: " + std::string(mode) + ".";
    return false;
  }
  return platform_->setWindowMode(parsed, error);
}

bool Bgfx2DAppHost::setMouseCaptured(const bool captured, std::string &error) {
  return platform_->setMouseCaptured(captured, error);
}

std::string Bgfx2DAppHost::clipboard() const { return platform_->clipboard(); }

bool Bgfx2DAppHost::setClipboard(const std::string &text, std::string &error) {
  return platform_->setClipboard(text, error);
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
  const platform::PlatformFrameState &state = platform_->frameState();
  const int drawableWidth = drawableDimension(state.width);
  const int drawableHeight = drawableDimension(state.height);
  if (drawableWidth != renderWidth_ || drawableHeight != renderHeight_) {
    if (!graphics_.resize(static_cast<std::uint32_t>(drawableWidth),
                          static_cast<std::uint32_t>(drawableHeight), error))
      return false;
    renderWidth_ = drawableWidth;
    renderHeight_ = drawableHeight;
  }

  graphics_.beginFrame(0x000000ffU);
  const bool began = renderer_->renderer.beginFrame(
      camera, cameraPosition, viewportDimension(drawableWidth),
      viewportDimension(drawableHeight), deltaSeconds, error);
  if (!began) {
    static_cast<void>(graphics_.endFrame());
    return false;
  }

  bool rendered = renderer_->renderer.drawWorld(world);
  if (rendered && navigation != nullptr)
    rendered = renderer_->renderer.drawNavigation(*navigation);
  if (rendered)
    rendered = renderer_->renderer.drawHud(world);
  std::string flushError;
  const bool flushed = renderer_->renderer.endFrame(flushError);
  static_cast<void>(graphics_.endFrame());
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
  return graphics_.rendererName();
}

} // namespace demi::runtime
