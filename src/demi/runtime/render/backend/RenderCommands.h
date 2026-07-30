#pragma once

#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/QuadBatch.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace demi::runtime::render {

struct View2DConfig {
  std::uint16_t id = 0;
  std::uint16_t width = 1;
  std::uint16_t height = 1;
  std::uint32_t clearRgba = 0x000000ffU;
  bool clear = true;
  FrameBufferHandle frameBuffer;
};

struct TransientDraw {
  std::uint16_t viewId = 0;
  std::span<const std::byte> vertices;
  VertexLayout vertexLayout;
  std::span<const std::uint16_t> indices;
  ProgramHandle program;
  TextureHandle texture;
  SamplerHandle sampler;
  BlendMode blend = BlendMode::Alpha;
  ScissorRect scissor;
};

class RenderCommands {
public:
  virtual ~RenderCommands() = default;
  RenderCommands(const RenderCommands &) = delete;
  RenderCommands &operator=(const RenderCommands &) = delete;

  [[nodiscard]] virtual bool configureView2D(const View2DConfig &view,
                                             std::string &error) = 0;
  [[nodiscard]] virtual bool submit(const TransientDraw &draw,
                                    std::string &error) = 0;

protected:
  RenderCommands() = default;
};

[[nodiscard]] std::unique_ptr<RenderCommands>
createBgfxRenderCommands(GpuResources &resources);

} // namespace demi::runtime::render
