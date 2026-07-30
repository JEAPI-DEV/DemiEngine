#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"

#include <bgfx/bgfx.h>

#include <limits>

namespace demi::runtime::render {

namespace {

constexpr bgfx::ViewId MainView = 0;

bgfx::RendererType::Enum rendererType(const GraphicsApi api) {
  switch (api) {
  case GraphicsApi::Automatic:
    return bgfx::RendererType::Count;
  case GraphicsApi::Vulkan:
    return bgfx::RendererType::Vulkan;
  case GraphicsApi::OpenGL:
    return bgfx::RendererType::OpenGL;
  case GraphicsApi::OpenGLES:
    return bgfx::RendererType::OpenGLES;
  case GraphicsApi::Noop:
    return bgfx::RendererType::Noop;
  }
  return bgfx::RendererType::Count;
}

bgfx::NativeWindowHandleType::Enum
nativeWindowType(const NativeWindowKind kind) {
  switch (kind) {
  case NativeWindowKind::Default:
    return bgfx::NativeWindowHandleType::Default;
  case NativeWindowKind::Wayland:
    return bgfx::NativeWindowHandleType::Wayland;
  }
  return bgfx::NativeWindowHandleType::Default;
}

bool validDimensions(const std::uint32_t width, const std::uint32_t height) {
  return width > 0 && height > 0 &&
         width <= std::numeric_limits<std::uint16_t>::max() &&
         height <= std::numeric_limits<std::uint16_t>::max();
}

} // namespace

BgfxGraphicsDevice::~BgfxGraphicsDevice() { shutdown(); }

bool BgfxGraphicsDevice::initialize(const GraphicsDeviceConfig &config,
                                    std::string &error) {
  if (initialized_) {
    error = "The bgfx graphics device is already initialized.";
    return false;
  }
  if (!validDimensions(config.width, config.height)) {
    error = "Graphics dimensions must be between 1 and 65535.";
    return false;
  }
  if (config.api != GraphicsApi::Noop &&
      config.nativeWindow.window == nullptr) {
    error = "A native window handle is required for a visible bgfx renderer.";
    return false;
  }

  bgfx::Init init;
  init.type = rendererType(config.api);
  init.resolution.width = config.width;
  init.resolution.height = config.height;
  init.resolution.reset = config.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
  init.platformData.ndt = config.nativeWindow.display;
  init.platformData.nwh = config.nativeWindow.window;
  init.platformData.context = config.nativeWindow.context;
  init.platformData.backBuffer = config.nativeWindow.backBuffer;
  init.platformData.backBufferDS = config.nativeWindow.backBufferDepthStencil;
  init.platformData.type = nativeWindowType(config.nativeWindow.kind);

  if (!bgfx::init(init)) {
    error = "bgfx initialization failed for the requested " +
            std::string(graphicsApiName(config.api)) + " renderer.";
    return false;
  }

  initialized_ = true;
  vsync_ = config.vsync;
  width_ = config.width;
  height_ = config.height;
  rendererName_ = bgfx::getRendererName(bgfx::getRendererType());
  noop_ = bgfx::getRendererType() == bgfx::RendererType::Noop;
  bgfx::setDebug(config.debug ? BGFX_DEBUG_TEXT : BGFX_DEBUG_NONE);
  return true;
}

void BgfxGraphicsDevice::shutdown() {
  if (!initialized_)
    return;
  bgfx::shutdown();
  initialized_ = false;
  noop_ = false;
  rendererName_.clear();
}

bool BgfxGraphicsDevice::resize(const std::uint32_t width,
                                const std::uint32_t height,
                                std::string &error) {
  if (!initialized_) {
    error = "Cannot resize an uninitialized bgfx graphics device.";
    return false;
  }
  if (!validDimensions(width, height)) {
    error = "Graphics dimensions must be between 1 and 65535.";
    return false;
  }
  if (width == width_ && height == height_)
    return true;

  width_ = width;
  height_ = height;
  // The Noop backend deliberately has no back buffer and asserts if reset is
  // asked to recreate one. It still tracks logical dimensions so headless
  // render-command tests exercise the same device contract.
  if (!noop_)
    bgfx::reset(width_, height_, resetFlags());
  return true;
}

void BgfxGraphicsDevice::beginFrame(const std::uint32_t rgba) {
  if (!initialized_)
    return;
  bgfx::setViewRect(MainView, 0, 0, bgfx::BackbufferRatio::Equal);
  bgfx::setViewClear(MainView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, rgba, 1.0F,
                     0);
  bgfx::touch(MainView);
}

std::uint32_t BgfxGraphicsDevice::endFrame() {
  if (!initialized_)
    return 0;
  return bgfx::frame();
}

std::uint32_t BgfxGraphicsDevice::resetFlags() const {
  return vsync_ ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
}

} // namespace demi::runtime::render
