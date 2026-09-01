#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"

#include "demi/runtime/diagnostics/DeviceLog.h"

#include <bgfx/bgfx.h>

#include <chrono>
#include <limits>
#include <ratio>
#include <string>
#include <thread>

#if defined(__ANDROID__)
extern "C" bool DemiAndroidSurfaceAvailable(void);
#endif

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

#if defined(__ANDROID__)
  // Android destroys ANativeWindow surfaces asynchronously. Running bgfx on
  // the SDL thread prevents queued frames from targeting an abandoned surface.
  (void)bgfx::renderFrame();
#endif
  bgfx::Init init;
  init.type = rendererType(config.api);
  init.resolution.width = config.width;
  init.resolution.height = config.height;
  init.resolution.reset = config.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
#if defined(__ANDROID__)
  // Android 15 Mali drivers reject BGRA8 swapchains when they validate
  // AHardwareBuffer-backed allocations, which surfaces as
  // VK_ERROR_SURFACE_LOST_KHR. RGBA8 is the native window format on Android.
  init.resolution.formatColor = bgfx::TextureFormat::RGBA8;
#endif
  init.platformData.ndt = config.nativeWindow.display;
  init.platformData.nwh = config.nativeWindow.window;
  init.platformData.context = config.nativeWindow.context;
  init.platformData.backBuffer = config.nativeWindow.backBuffer;
  init.platformData.backBufferDS = config.nativeWindow.backBufferDepthStencil;
  init.platformData.type = nativeWindowType(config.nativeWindow.kind);

  // Android destroys ANativeWindow surfaces asynchronously, so an init can
  // race the window lifecycle (VK_ERROR_SURFACE_LOST_KHR). bgfx also falls
  // back to a lower-scored renderer when the requested one fails, which would
  // silently trade the backend away. Both are treated as attempt failures and
  // retried with a delay; only a matching backend counts as initialized.
#if defined(__ANDROID__)
  constexpr int kInitAttempts = 5;
#else
  constexpr int kInitAttempts = 1;
#endif
  const bgfx::RendererType::Enum requested = init.type;
  bool ready = false;
  std::string lastError;
  for (int attempt = 1; attempt <= kInitAttempts && !ready; ++attempt) {
    if (attempt > 1) {
      deviceLog(deviceLogMessage(
          "render", "Retrying bgfx init (attempt " + std::to_string(attempt) +
                        " of " + std::to_string(kInitAttempts) + ")."));
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!bgfx::init(init)) {
      lastError = "bgfx initialization failed for the requested " +
                  std::string(graphicsApiName(config.api)) + " renderer.";
      deviceLogError(deviceLogMessage(
          "render", lastError + " Attempt " + std::to_string(attempt) + "."));
      continue;
    }
    const bgfx::RendererType::Enum actual = bgfx::getRendererType();
    if (requested != bgfx::RendererType::Count && actual != requested) {
      lastError =
          std::string("bgfx silently fell back from the requested ") +
          std::string(graphicsApiName(config.api)) + " renderer to " +
          std::string(bgfx::getRendererName(actual)) + ".";
      deviceLogError(deviceLogMessage(
          "render", lastError + " Attempt " + std::to_string(attempt) + "."));
      bgfx::shutdown();
      continue;
    }
    ready = true;
  }
  if (!ready) {
    error = lastError.empty()
                ? "bgfx initialization failed for the requested " +
                      std::string(graphicsApiName(config.api)) + " renderer."
                : lastError;
    return false;
  }

  initialized_ = true;
  vsync_ = config.vsync;
  width_ = config.width;
  height_ = config.height;
  rendererName_ = bgfx::getRendererName(bgfx::getRendererType());
  noop_ = bgfx::getRendererType() == bgfx::RendererType::Noop;
  bgfx::setDebug(config.debug ? BGFX_DEBUG_TEXT : BGFX_DEBUG_NONE);
  deviceLog(deviceLogMessage(
      "render",
      "bgfx initialized: requested " + std::string(graphicsApiName(config.api)) +
          ", backend " + std::string(rendererName_) + ", " +
          std::to_string(config.width) + "x" + std::to_string(config.height) +
          ", native window " + devicePointerText(config.nativeWindow.window) +
          "."));
  return true;
}

bool BgfxGraphicsDevice::updateNativeWindow(const NativeWindowHandle handle,
                                            std::string &error) {
  if (!initialized_ || noop_) {
    error = "Cannot update the native window of an unavailable renderer.";
    return false;
  }
  if (handle.window == nullptr) {
    error = "A valid native window is required before rendering resumes.";
    return false;
  }
  deviceLog(deviceLogMessage(
      "render",
      "Rebinding bgfx to native window " + devicePointerText(handle.window) +
          "."));
  bgfx::PlatformData data;
  data.ndt = handle.display;
  data.nwh = handle.window;
  data.context = handle.context;
  data.backBuffer = handle.backBuffer;
  data.backBufferDS = handle.backBufferDepthStencil;
  data.type = nativeWindowType(handle.kind);
  bgfx::setPlatformData(data);
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
#if defined(__ANDROID__)
  // Diagnostics-only rate limiter: the flag suppresses a log per transition;
  // endFrame runs on the single render thread, so one slot per device is safe.
  static bool surfaceSkipLogged = false;
  if (!DemiAndroidSurfaceAvailable()) {
    if (!surfaceSkipLogged) {
      surfaceSkipLogged = true;
      deviceLogError(deviceLogMessage(
          "render",
          "bgfx::frame skipped because the Android surface is unavailable."));
    }
    return 0;
  }
  surfaceSkipLogged = false;
#endif
  return bgfx::frame();
}

std::uint32_t BgfxGraphicsDevice::resetFlags() const {
  return vsync_ ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
}

} // namespace demi::runtime::render
