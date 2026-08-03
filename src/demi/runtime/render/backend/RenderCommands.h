#pragma once

#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/QuadBatch.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace demi::runtime::render {

struct View2DConfig {
  std::uint16_t id = 0;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t width = 1;
  std::uint16_t height = 1;
  std::uint32_t clearRgba = 0x000000ffU;
  bool clear = true;
  FrameBufferHandle frameBuffer;
};

struct Point3D {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct View3DConfig {
  std::uint16_t id = 0;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t width = 1;
  std::uint16_t height = 1;
  std::uint32_t clearRgba = 0x000000ffU;
  Point3D eye;
  Point3D target{0.0F, 0.0F, 1.0F};
  Point3D up{0.0F, 1.0F, 0.0F};
  float verticalFovDegrees = 60.0F;
  float nearClip = 0.05F;
  float farClip = 500.0F;
  float orthographicSize = 10.0F;
  bool perspective = true;
  bool clearColor = true;
  bool clearDepth = true;
  FrameBufferHandle frameBuffer;
};

enum class DepthTest { Disabled, Less, LessEqual, Always };
enum class CullMode { None, Clockwise, CounterClockwise };
enum class PrimitiveTopology { Triangles, Lines, Points };

struct DrawState {
  BlendMode blend = BlendMode::Opaque;
  DepthTest depthTest = DepthTest::Disabled;
  CullMode cull = CullMode::None;
  PrimitiveTopology topology = PrimitiveTopology::Triangles;
  bool writeColor = true;
  bool writeAlpha = true;
  bool writeDepth = false;
  bool multisampling = true;
};

struct DrawUniformValue {
  UniformHandle handle;
  std::span<const float> values;
  std::uint16_t count = 1;
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
  DrawState state;
  ScissorRect scissor;
  std::span<const DrawUniformValue> uniforms;
};

struct BufferedDraw {
  std::uint16_t viewId = 0;
  BufferSlice vertices;
  BufferSlice indices;
  ProgramHandle program;
  TextureHandle texture;
  SamplerHandle sampler;
  DrawState state;
  ScissorRect scissor;
  std::array<float, 16> transform{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
                                  0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
                                  0.0F, 0.0F, 0.0F, 1.0F};
  std::span<const DrawUniformValue> uniforms;
};

struct InstancedBufferedDraw {
  std::uint16_t viewId = 0;
  BufferSlice vertices;
  BufferSlice indices;
  ProgramHandle program;
  TextureHandle texture;
  SamplerHandle sampler;
  DrawState state;
  ScissorRect scissor;
  std::span<const std::array<float, 16>> transforms;
  std::span<const DrawUniformValue> uniforms;
};

class RenderCommands {
public:
  virtual ~RenderCommands() = default;
  RenderCommands(const RenderCommands &) = delete;
  RenderCommands &operator=(const RenderCommands &) = delete;

  [[nodiscard]] virtual bool configureView2D(const View2DConfig &view,
                                             std::string &error) = 0;
  [[nodiscard]] virtual bool configureView3D(const View3DConfig &view,
                                             std::string &error) = 0;
  [[nodiscard]] virtual bool submit(const TransientDraw &draw,
                                    std::string &error) = 0;
  [[nodiscard]] virtual bool submit(const BufferedDraw &draw,
                                    std::string &error) = 0;
  [[nodiscard]] virtual bool submit(const InstancedBufferedDraw &draw,
                                    std::string &error) = 0;

protected:
  RenderCommands() = default;
};

[[nodiscard]] std::unique_ptr<RenderCommands>
createBgfxRenderCommands(GpuResources &resources);

} // namespace demi::runtime::render
