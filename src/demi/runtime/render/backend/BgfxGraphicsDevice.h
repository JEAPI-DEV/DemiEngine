#pragma once

#include "demi/runtime/render/backend/GraphicsDevice.h"

#include <cstdint>
#include <string>

namespace demi::runtime::render {

class BgfxGraphicsDevice final : public GraphicsDevice {
public:
  BgfxGraphicsDevice() = default;
  ~BgfxGraphicsDevice() override;

  BgfxGraphicsDevice(const BgfxGraphicsDevice &) = delete;
  BgfxGraphicsDevice &operator=(const BgfxGraphicsDevice &) = delete;

  [[nodiscard]] bool initialize(const GraphicsDeviceConfig &config,
                                std::string &error) override;
  void shutdown() override;
  [[nodiscard]] bool resize(std::uint32_t width, std::uint32_t height,
                            std::string &error) override;
  void beginFrame(std::uint32_t rgba) override;
  [[nodiscard]] std::uint32_t endFrame() override;

  [[nodiscard]] bool initialized() const override { return initialized_; }
  [[nodiscard]] std::string_view rendererName() const override {
    return rendererName_;
  }

private:
  [[nodiscard]] std::uint32_t resetFlags() const;

  bool initialized_ = false;
  bool noop_ = false;
  bool vsync_ = true;
  std::uint32_t width_ = 1;
  std::uint32_t height_ = 1;
  std::string rendererName_;
};

} // namespace demi::runtime::render
