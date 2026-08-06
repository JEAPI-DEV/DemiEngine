#include "demi/runtime/app/BgfxAppContext.h"

#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/RenderCommands.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace demi::runtime {
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

int drawableDimension(const int value) { return std::max(value, 1); }

std::uint16_t viewportDimension(const int value) {
  return static_cast<std::uint16_t>(std::clamp(
      value, 1, static_cast<int>(std::numeric_limits<std::uint16_t>::max())));
}

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

BgfxAppContext::BgfxAppContext()
    : platform_(platform::createSdlPlatformHost()) {}

BgfxAppContext::~BgfxAppContext() { shutdown(); }

bool BgfxAppContext::initialize(const BgfxAppContextConfig &config,
                                std::string &error) {
  if (initialized_) {
    error = "The bgfx application context is already initialized.";
    return false;
  }
  if (!platform_->initialize({.title = config.title,
                              .width = config.width,
                              .height = config.height,
                              .resizable = true},
                             error))
    return false;
  const auto &state = platform_->frameState();
  renderWidth_ = drawableDimension(state.width);
  renderHeight_ = drawableDimension(state.height);
  if (!graphics_.initialize(
          {.api = config.graphicsApi,
           .nativeWindow = platform_->nativeWindow(),
           .width = static_cast<std::uint32_t>(renderWidth_),
           .height = static_cast<std::uint32_t>(renderHeight_),
           .vsync = config.vsync,
           .debug = config.debugGraphics},
          error)) {
    platform_->shutdown();
    renderWidth_ = 0;
    renderHeight_ = 0;
    return false;
  }
  resources_ = render::createBgfxGpuResources();
  commands_ = render::createBgfxRenderCommands(*resources_);
  if (commands_ == nullptr) {
    error = "Could not create bgfx render commands.";
    shutdown();
    return false;
  }
  initialized_ = true;
  return true;
}

void BgfxAppContext::shutdown() {
  if (frameOpen_)
    endFrame();
  commands_.reset();
  if (resources_ != nullptr)
    resources_->clear();
  resources_.reset();
  graphics_.shutdown();
  if (platform_ != nullptr)
    platform_->shutdown();
  renderWidth_ = 0;
  renderHeight_ = 0;
  initialized_ = false;
}

bool BgfxAppContext::beginFrame(std::string &error) {
  if (!initialized_) {
    error = "The bgfx application context is not initialized.";
    return false;
  }
  if (frameOpen_) {
    error = "The bgfx application context already has an open frame.";
    return false;
  }
  const auto &state = platform_->frameState();
  const int width = drawableDimension(state.width);
  const int height = drawableDimension(state.height);
  if ((width != renderWidth_ || height != renderHeight_) &&
      !graphics_.resize(static_cast<std::uint32_t>(width),
                        static_cast<std::uint32_t>(height), error))
    return false;
  renderWidth_ = width;
  renderHeight_ = height;
  graphics_.beginFrame(0x000000ffU);
  frameOpen_ = true;
  return true;
}

void BgfxAppContext::endFrame() {
  if (!frameOpen_)
    return;
  static_cast<void>(graphics_.endFrame());
  frameOpen_ = false;
}

void BgfxAppContext::poll(InputState &input) { platform_->poll(input); }

const platform::PlatformFrameState &BgfxAppContext::frameState() const {
  return platform_->frameState();
}

bool BgfxAppContext::setWindowMode(const std::string_view mode,
                                   std::string &error) {
  bool valid = false;
  const auto parsed = windowMode(mode, valid);
  if (!valid) {
    error = "Unknown window mode: " + std::string(mode) + ".";
    return false;
  }
  return platform_->setWindowMode(parsed, error);
}

bool BgfxAppContext::setMouseCaptured(const bool captured, std::string &error) {
  return platform_->setMouseCaptured(captured, error);
}

std::string BgfxAppContext::clipboard() const { return platform_->clipboard(); }

bool BgfxAppContext::setClipboard(const std::string &text, std::string &error) {
  return platform_->setClipboard(text, error);
}

std::string_view BgfxAppContext::rendererName() const {
  return graphics_.rendererName();
}

render::GpuResources *BgfxAppContext::resources() const {
  return resources_.get();
}

render::RenderCommands *BgfxAppContext::commands() const {
  return commands_.get();
}

std::uint16_t BgfxAppContext::viewportWidth() const {
  return viewportDimension(renderWidth_);
}

std::uint16_t BgfxAppContext::viewportHeight() const {
  return viewportDimension(renderHeight_);
}

} // namespace demi::runtime
