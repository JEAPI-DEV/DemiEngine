#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace demi::runtime::render {

enum class GraphicsApi {
  Automatic,
  Vulkan,
  OpenGL,
  OpenGLES,
  Noop,
};

enum class NativeWindowKind {
  Default,
  Wayland,
};

struct NativeWindowHandle {
  void *display = nullptr;
  void *window = nullptr;
  void *context = nullptr;
  void *backBuffer = nullptr;
  void *backBufferDepthStencil = nullptr;
  NativeWindowKind kind = NativeWindowKind::Default;
};

struct GraphicsDeviceConfig {
  GraphicsApi api = GraphicsApi::Automatic;
  NativeWindowHandle nativeWindow;
  std::uint32_t width = 1;
  std::uint32_t height = 1;
  bool vsync = true;
  bool debug = false;
};

class GraphicsDevice {
public:
  virtual ~GraphicsDevice() = default;

  GraphicsDevice(const GraphicsDevice &) = delete;
  GraphicsDevice &operator=(const GraphicsDevice &) = delete;

  [[nodiscard]] virtual bool initialize(const GraphicsDeviceConfig &config,
                                        std::string &error) = 0;
  virtual void shutdown() = 0;
  [[nodiscard]] virtual bool resize(std::uint32_t width, std::uint32_t height,
                                    std::string &error) = 0;
  virtual void beginFrame(std::uint32_t rgba) = 0;
  [[nodiscard]] virtual std::uint32_t endFrame() = 0;

  [[nodiscard]] virtual bool initialized() const = 0;
  [[nodiscard]] virtual std::string_view rendererName() const = 0;

protected:
  GraphicsDevice() = default;
};

[[nodiscard]] std::string_view graphicsApiName(GraphicsApi api);
[[nodiscard]] bool parseGraphicsApi(std::string_view name, GraphicsApi &api);

} // namespace demi::runtime::render
