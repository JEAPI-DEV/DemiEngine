#pragma once

#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/BgfxRenderCommandsOptions.h"
#include "demi/runtime/render/backend/QuadBatch.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

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
  // Synchronous diagnostic label; the backend copies it during configuration.
  std::string_view debugName;
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
  // Nonzero forward direction, independent of the eye position.
  Point3D forward{0.0F, 0.0F, 1.0F};
  Point3D up{0.0F, 1.0F, 0.0F};
  float verticalFovDegrees = 60.0F;
  float nearClip = 0.05F;
  float farClip = 500.0F;
  float orthographicSize = 10.0F;
  bool perspective = true;
  bool clearColor = true;
  bool clearDepth = true;
  FrameBufferHandle frameBuffer;
  bool sequential = false;
  std::string_view debugName;
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

struct DrawTextureBinding {
  std::uint8_t stage = 1;
  TextureHandle texture;
  SamplerHandle sampler;
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
  std::span<const DrawTextureBinding> textures;
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
  std::span<const DrawTextureBinding> textures;
};

// Synchronous borrowed records. Native instance buffers require a nonzero
// stride divisible by sixteen; records remain ordinary typed author data.
struct InstanceDataView {
  std::span<const std::byte> data;
  std::uint16_t stride = 0;
};

template <typename Record, std::size_t Extent>
[[nodiscard]] InstanceDataView instanceData(std::span<Record, Extent> records) {
  static_assert(std::is_trivially_copyable_v<Record>);
  static_assert(sizeof(Record) <= UINT16_MAX);
  static_assert(sizeof(Record) % 16 == 0);
  return {.data = std::as_bytes(records),
          .stride = static_cast<std::uint16_t>(sizeof(Record))};
}

struct InstancedBufferedDraw {
  std::uint16_t viewId = 0;
  BufferSlice vertices;
  BufferSlice indices;
  ProgramHandle program;
  TextureHandle texture;
  SamplerHandle sampler;
  DrawState state;
  ScissorRect scissor;
  InstanceDataView instances;
  std::span<const DrawUniformValue> uniforms;
  std::span<const DrawTextureBinding> textures;
};

struct InstanceBatchSubmission {
  std::size_t submittedRecords = 0;
  std::size_t submittedBatches = 0;
  bool complete = false;
};

class RenderCommands {
public:
  virtual ~RenderCommands() = default;
  RenderCommands(const RenderCommands &) = delete;
  RenderCommands &operator=(const RenderCommands &) = delete;

  // Largest immediately available instance chunk. Native implementations may
  // use dedicated frame upload arenas so UI transient vertices remain intact.
  // Query immediately before submission; other draws consume frame capacity.
  [[nodiscard]] virtual std::uint32_t availableInstances(std::uint16_t,
                                                         std::uint32_t) const {
    return 0;
  }

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
  // Copies resolved texture contents between distinct resources in an ordered
  // view. The destination must have been created with blitDestination enabled.
  [[nodiscard]] virtual bool copyTexture(std::uint16_t, TextureHandle,
                                         TextureHandle, std::uint16_t,
                                         std::uint16_t, std::string &error) {
    error = "This renderer cannot capture scene textures.";
    return false;
  }
  // Submits legal prefixes using current transient availability. A failure
  // returns the already-submitted prefix so callers never redraw it by
  // accident.
  [[nodiscard]] InstanceBatchSubmission
  submitInstanceBatches(const InstancedBufferedDraw &draw, std::string &error);

protected:
  RenderCommands() = default;
};

[[nodiscard]] std::unique_ptr<RenderCommands>
createBgfxRenderCommands(GpuResources &resources,
                         const BgfxRenderCommandsOptions &options = {},
                         std::string *error = nullptr);

} // namespace demi::runtime::render
