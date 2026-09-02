#include "demi/runtime/app/BgfxAppContext.h"

#include "demi/runtime/diagnostics/DeviceLog.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/RenderCommands.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>

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
#if defined(__ANDROID__)
  if (configured == nullptr || *configured == '\0') {
    deviceLog(deviceLogMessage(
        "render",
        std::string("Graphics api resolved to ") +
            std::string(render::graphicsApiName(render::GraphicsApi::Vulkan)) +
            " (Android default)."));
    return render::GraphicsApi::Vulkan;
  }
#endif
  if (configured == nullptr || *configured == '\0')
    return render::GraphicsApi::Automatic;
  render::GraphicsApi api = render::GraphicsApi::Automatic;
  const bool recognised = render::parseGraphicsApi(configured, api);
  if (!recognised)
    api = render::GraphicsApi::Automatic;
  deviceLog(deviceLogMessage(
      "render",
      std::string("Graphics api resolved to ") +
          std::string(render::graphicsApiName(api)) +
          " (DEMI_GRAPHICS_API=" + configured + ")" +
          (recognised ? "" : "; the value is unrecognised and was ignored") +
          "."));
  return api;
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
  surfaceGeneration_ = state.surfaceGeneration;
  renderWidth_ = drawableDimension(state.width);
  renderHeight_ = drawableDimension(state.height);
  const auto tryInitializeGraphics = [&](render::GraphicsApi api,
                                         std::string &failure) {
    return graphics_.initialize(
        {.api = api,
         .nativeWindow = platform_->nativeWindow(),
         .width = static_cast<std::uint32_t>(renderWidth_),
         .height = static_cast<std::uint32_t>(renderHeight_),
         .vsync = config.vsync,
         .debug = config.debugGraphics},
        failure);
  };
  if (!tryInitializeGraphics(config.graphicsApi, error)) {
#if defined(__ANDROID__)
    const bool canFallBack = config.graphicsApi == render::GraphicsApi::Vulkan;
    if (canFallBack) {
      deviceLogError(
          deviceLogMessage("render", error + " Falling back to OpenGL ES."));
      std::string fallbackError;
      if (!tryInitializeGraphics(render::GraphicsApi::OpenGLES,
                                 fallbackError)) {
        error = fallbackError;
      }
    }
#endif
    if (!graphics_.initialized()) {
      deviceLogError(deviceLogMessage("render", error));
      platform_->shutdown();
      renderWidth_ = 0;
      renderHeight_ = 0;
      return false;
    }
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
  surfaceGeneration_ = 0;
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
  if (!state.drawableAvailable) {
    error = "The platform drawable is temporarily unavailable.";
    deviceLogError(deviceLogMessage("render", error));
    return false;
  }
  if (state.surfaceGeneration != surfaceGeneration_) {
    if (!graphics_.updateNativeWindow(platform_->nativeWindow(), error)) {
      deviceLogError(deviceLogMessage("render", error));
      return false;
    }
    surfaceGeneration_ = state.surfaceGeneration;
  }
  const int width = drawableDimension(state.width);
  const int height = drawableDimension(state.height);
  if ((width != renderWidth_ || height != renderHeight_)) {
    deviceLog(deviceLogMessage(
        "render", "Back buffer resize " + std::to_string(renderWidth_) + "x" +
                      std::to_string(renderHeight_) + " -> " +
                      std::to_string(width) + "x" + std::to_string(height) +
                      "."));
    if (!graphics_.resize(static_cast<std::uint32_t>(width),
                          static_cast<std::uint32_t>(height), error)) {
      deviceLogError(deviceLogMessage("render", error));
      return false;
    }
  }
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

bool BgfxAppContext::requestFrameRate(const float framesPerSecond) {
  return platform_->requestFrameRate(framesPerSecond);
}

std::string BgfxAppContext::clipboard() const { return platform_->clipboard(); }

bool BgfxAppContext::setClipboard(const std::string &text, std::string &error) {
  return platform_->setClipboard(text, error);
}

bool BgfxAppContext::requestPermission(const std::string &permission,
                                       std::function<void(bool, bool)> result,
                                       std::string &error) {
  return platform_->requestPermission(permission, std::move(result), error);
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
