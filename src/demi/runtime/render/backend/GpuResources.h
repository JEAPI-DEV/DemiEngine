#pragma once

#include "demi/runtime/render/backend/ResourceHandle.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime::render {

struct TextureTag;
struct SamplerTag;
struct BufferTag;
struct ProgramTag;
struct FrameBufferTag;
struct UniformTag;

using TextureHandle = ResourceHandle<TextureTag>;
using SamplerHandle = ResourceHandle<SamplerTag>;
using BufferHandle = ResourceHandle<BufferTag>;
using ProgramHandle = ResourceHandle<ProgramTag>;
using FrameBufferHandle = ResourceHandle<FrameBufferTag>;
using UniformHandle = ResourceHandle<UniformTag>;

enum class TextureFormat { R8, RGBA8, BGRA8 };
enum class TextureFilter { Linear, Nearest };
enum class TextureWrap { Clamp, Repeat, Mirror };
enum class BufferKind { Vertex, Index16, Index32 };
enum class BuiltinProgram {
  Textured2D,
  VertexColor3D,
  Textured3D,
  Lit3D,
  Lit3DInstanced,
  PostProcess2D
};
enum class UniformType { Vec4, Matrix3, Matrix4 };
enum class VertexSemantic {
  Position,
  Normal,
  Tangent,
  Bitangent,
  Color0,
  Color1,
  TexCoord0,
  TexCoord1,
  TexCoord2,
  TexCoord3,
  Indices,
  Weight
};
enum class VertexElementType { UInt8, Int16, Half, Float };

struct VertexAttribute {
  VertexSemantic semantic = VertexSemantic::Position;
  std::uint8_t components = 0;
  VertexElementType type = VertexElementType::Float;
  bool normalized = false;
  bool asInteger = false;
};

struct VertexLayout {
  std::vector<VertexAttribute> attributes;
};

struct TextureCreateInfo {
  std::uint16_t width = 1;
  std::uint16_t height = 1;
  TextureFormat format = TextureFormat::RGBA8;
  std::span<const std::byte> data;
  bool renderTarget = false;
  bool generateMipmaps = false;
  TextureFilter filter = TextureFilter::Linear;
  TextureWrap wrap = TextureWrap::Clamp;
  std::string debugName;
};

struct TextureUpdateInfo {
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::span<const std::byte> data;
};

struct BufferCreateInfo {
  BufferKind kind = BufferKind::Vertex;
  std::span<const std::byte> data;
  VertexLayout vertexLayout;
  std::string debugName;
};

struct ProgramCreateInfo {
  std::span<const std::byte> vertexShader;
  std::span<const std::byte> fragmentShader;
  std::string debugName;
};

struct RenderTargetCreateInfo {
  std::uint16_t width = 1;
  std::uint16_t height = 1;
  TextureFormat colorFormat = TextureFormat::RGBA8;
  bool depth = true;
  std::string debugName;
};

struct RenderTargetHandles {
  FrameBufferHandle frameBuffer;
  TextureHandle color;
  TextureHandle depth;
};

struct BufferSlice {
  BufferHandle handle;
  std::uint32_t first = 0;
  std::uint32_t count = UINT32_MAX;
};

class GpuResources {
public:
  virtual ~GpuResources() = default;
  GpuResources(const GpuResources &) = delete;
  GpuResources &operator=(const GpuResources &) = delete;

  [[nodiscard]] virtual TextureHandle
  createTexture(const TextureCreateInfo &info, std::string &error) = 0;
  [[nodiscard]] virtual bool updateTexture(TextureHandle,
                                           const TextureUpdateInfo &,
                                           std::string &error) {
    error = "This graphics resource backend cannot update textures.";
    return false;
  }
  [[nodiscard]] virtual SamplerHandle createSampler(std::string_view name,
                                                    std::string &error) = 0;
  [[nodiscard]] virtual UniformHandle createUniform(std::string_view name,
                                                    UniformType type,
                                                    std::uint16_t count,
                                                    std::string &error) = 0;
  [[nodiscard]] virtual BufferHandle createBuffer(const BufferCreateInfo &info,
                                                  std::string &error) = 0;
  [[nodiscard]] virtual ProgramHandle
  createProgram(const ProgramCreateInfo &info, std::string &error) = 0;
  [[nodiscard]] virtual ProgramHandle
  createBuiltinProgram(BuiltinProgram program, std::string &error) = 0;
  // Stable name used by cooked shader manifests (vulkan, opengl, opengles,
  // or noop). Keeping this query here prevents material code from depending
  // on bgfx renderer enums.
  [[nodiscard]] virtual std::string_view shaderBackend() const = 0;
  [[nodiscard]] virtual RenderTargetHandles
  createRenderTarget(const RenderTargetCreateInfo &info,
                     std::string &error) = 0;

  virtual bool destroy(TextureHandle handle) = 0;
  virtual bool destroy(SamplerHandle handle) = 0;
  virtual bool destroy(UniformHandle handle) = 0;
  virtual bool destroy(BufferHandle handle) = 0;
  virtual bool destroy(ProgramHandle handle) = 0;
  virtual bool destroy(FrameBufferHandle handle) = 0;
  virtual void clear() = 0;

protected:
  GpuResources() = default;
};

[[nodiscard]] std::unique_ptr<GpuResources> createBgfxGpuResources();

} // namespace demi::runtime::render
