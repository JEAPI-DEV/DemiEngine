#pragma once

#include "demi/runtime/render/backend/GraphicsDevice.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <memory>
#include <string>

namespace demi::runtime::platform {

enum class WindowMode { Windowed, Borderless, Fullscreen };

struct PlatformHostConfig {
  std::string title;
  int width = 960;
  int height = 540;
  bool resizable = true;
};

struct PlatformFrameState {
  int width = 1;
  int height = 1;
  float logicalDpi = 96.0F;
  float deltaSeconds = 0.0F;
  bool focused = true;
  bool minimized = false;
  bool suspended = false;
  bool quitRequested = false;
  unsigned lowMemorySignals = 0;
};

class PlatformHost {
public:
  virtual ~PlatformHost() = default;

  PlatformHost(const PlatformHost &) = delete;
  PlatformHost &operator=(const PlatformHost &) = delete;

  [[nodiscard]] virtual bool initialize(const PlatformHostConfig &config,
                                        std::string &error) = 0;
  virtual void shutdown() = 0;
  virtual void poll(InputState &input) = 0;

  [[nodiscard]] virtual const PlatformFrameState &frameState() const = 0;
  [[nodiscard]] virtual render::NativeWindowHandle nativeWindow() const = 0;
  [[nodiscard]] virtual bool setWindowMode(WindowMode mode,
                                           std::string &error) = 0;
  [[nodiscard]] virtual bool setMouseCaptured(bool captured,
                                              std::string &error) = 0;
  [[nodiscard]] virtual std::string clipboard() const = 0;
  [[nodiscard]] virtual bool setClipboard(const std::string &text,
                                          std::string &error) = 0;

protected:
  PlatformHost() = default;
};

[[nodiscard]] std::unique_ptr<PlatformHost> createSdlPlatformHost();

} // namespace demi::runtime::platform
