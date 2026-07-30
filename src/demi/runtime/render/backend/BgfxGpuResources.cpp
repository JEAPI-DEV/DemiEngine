#include "demi/runtime/render/backend/BgfxResourceLookup.h"
#include "demi/runtime/render/backend/BgfxVertexLayout.h"
#include "demi/runtime/render/backend/GpuResources.h"

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>

#include <fs_ocornut_imgui.bin.h>
#include <vs_ocornut_imgui.bin.h>

#include <array>
#include <limits>

namespace demi::runtime::render {
namespace {

constexpr std::uint16_t Invalid = std::numeric_limits<std::uint16_t>::max();

const bgfx::EmbeddedShader EmbeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER_END(),
};

bgfx::TextureFormat::Enum textureFormat(const TextureFormat format) {
  switch (format) {
  case TextureFormat::R8:
    return bgfx::TextureFormat::R8;
  case TextureFormat::RGBA8:
    return bgfx::TextureFormat::RGBA8;
  case TextureFormat::BGRA8:
    return bgfx::TextureFormat::BGRA8;
  }
  return bgfx::TextureFormat::RGBA8;
}

std::uint64_t textureFlags(const TextureCreateInfo &info) {
  std::uint64_t flags = info.renderTarget ? BGFX_TEXTURE_RT : BGFX_TEXTURE_NONE;
  if (info.filter == TextureFilter::Nearest)
    flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
             BGFX_SAMPLER_MIP_POINT;
  switch (info.wrap) {
  case TextureWrap::Clamp:
    flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    break;
  case TextureWrap::Repeat:
    break;
  case TextureWrap::Mirror:
    flags |=
        BGFX_SAMPLER_U_MIRROR | BGFX_SAMPLER_V_MIRROR | BGFX_SAMPLER_W_MIRROR;
    break;
  }
  return flags;
}

const bgfx::Memory *copy(std::span<const std::byte> bytes) {
  return bytes.empty() ? nullptr
                       : bgfx::copy(bytes.data(),
                                    static_cast<std::uint32_t>(bytes.size()));
}

struct BufferValue {
  std::uint16_t index = Invalid;
  BufferKind kind = BufferKind::Vertex;
};

class BgfxGpuResources final : public GpuResources, public BgfxResourceLookup {
public:
  ~BgfxGpuResources() override { clear(); }

  TextureHandle createTexture(const TextureCreateInfo &info,
                              std::string &error) override {
    if (info.width == 0 || info.height == 0) {
      error = "Texture dimensions must be positive.";
      return {};
    }
    const bgfx::TextureHandle texture = bgfx::createTexture2D(
        info.width, info.height, info.generateMipmaps, 1,
        textureFormat(info.format), textureFlags(info), copy(info.data));
    if (!bgfx::isValid(texture)) {
      error = "bgfx could not create texture " + info.debugName + ".";
      return {};
    }
    if (!info.debugName.empty())
      bgfx::setName(texture, info.debugName.c_str());
    return textures_.insert(texture.idx);
  }

  SamplerHandle createSampler(const std::string_view name,
                              std::string &error) override {
    if (name.empty()) {
      error = "Sampler names must not be empty.";
      return {};
    }
    const bgfx::UniformHandle sampler = bgfx::createUniform(
        std::string(name).c_str(), bgfx::UniformType::Sampler);
    if (!bgfx::isValid(sampler)) {
      error = "bgfx could not create sampler " + std::string(name) + ".";
      return {};
    }
    return samplers_.insert(sampler.idx);
  }

  BufferHandle createBuffer(const BufferCreateInfo &info,
                            std::string &error) override {
    if (info.data.empty()) {
      error = "GPU buffer data must not be empty.";
      return {};
    }
    BufferValue value{.kind = info.kind};
    if (info.kind == BufferKind::Vertex) {
      bgfx::VertexLayout layout;
      if (!buildBgfxVertexLayout(info.vertexLayout, layout, error))
        return {};
      if (info.data.size() % layout.getStride() != 0) {
        error = "Vertex buffer byte count must be a multiple of its layout "
                "stride.";
        return {};
      }
      const bgfx::VertexBufferHandle buffer =
          bgfx::createVertexBuffer(copy(info.data), layout);
      if (bgfx::isValid(buffer))
        value.index = buffer.idx;
    } else {
      const std::uint16_t flags =
          info.kind == BufferKind::Index32 ? BGFX_BUFFER_INDEX32 : 0;
      const bgfx::IndexBufferHandle buffer =
          bgfx::createIndexBuffer(copy(info.data), flags);
      if (bgfx::isValid(buffer))
        value.index = buffer.idx;
    }
    if (value.index == Invalid) {
      error = "bgfx could not create buffer " + info.debugName + ".";
      return {};
    }
    return buffers_.insert(value);
  }

  ProgramHandle createProgram(const ProgramCreateInfo &info,
                              std::string &error) override {
    if (info.vertexShader.empty() || info.fragmentShader.empty()) {
      error = "A GPU program requires cooked vertex and fragment shaders.";
      return {};
    }
    const bgfx::ShaderHandle vertex =
        bgfx::createShader(copy(info.vertexShader));
    const bgfx::ShaderHandle fragment =
        bgfx::createShader(copy(info.fragmentShader));
    if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
      if (bgfx::isValid(vertex))
        bgfx::destroy(vertex);
      if (bgfx::isValid(fragment))
        bgfx::destroy(fragment);
      error = "bgfx rejected cooked shader data for " + info.debugName + ".";
      return {};
    }
    const bgfx::ProgramHandle program =
        bgfx::createProgram(vertex, fragment, true);
    if (!bgfx::isValid(program)) {
      error = "bgfx could not link program " + info.debugName + ".";
      return {};
    }
    return programs_.insert(program.idx);
  }

  ProgramHandle createBuiltinProgram(const BuiltinProgram program,
                                     std::string &error) override {
    const char *vertexName = nullptr;
    const char *fragmentName = nullptr;
    switch (program) {
    case BuiltinProgram::Textured2D:
      vertexName = "vs_ocornut_imgui";
      fragmentName = "fs_ocornut_imgui";
      break;
    }
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
    const bgfx::ShaderHandle vertex =
        bgfx::createEmbeddedShader(EmbeddedShaders, renderer, vertexName);
    const bgfx::ShaderHandle fragment =
        bgfx::createEmbeddedShader(EmbeddedShaders, renderer, fragmentName);
    if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
      if (bgfx::isValid(vertex))
        bgfx::destroy(vertex);
      if (bgfx::isValid(fragment))
        bgfx::destroy(fragment);
      error = "bgfx could not create the requested built-in program.";
      return {};
    }
    const bgfx::ProgramHandle handle =
        bgfx::createProgram(vertex, fragment, true);
    if (!bgfx::isValid(handle)) {
      error = "bgfx could not link the requested built-in program.";
      return {};
    }
    return programs_.insert(handle.idx);
  }

  RenderTargetHandles createRenderTarget(const RenderTargetCreateInfo &info,
                                         std::string &error) override {
    TextureCreateInfo colorInfo{.width = info.width,
                                .height = info.height,
                                .format = info.colorFormat,
                                .data = {},
                                .renderTarget = true,
                                .generateMipmaps = false,
                                .debugName = info.debugName + ".color"};
    const TextureHandle color = createTexture(colorInfo, error);
    if (!color)
      return {};
    const std::uint16_t *colorIndex = textures_.find(color);
    std::array<bgfx::Attachment, 2> attachments;
    attachments[0].init(bgfx::TextureHandle{*colorIndex}, bgfx::Access::Write,
                        0, 1, 0, BGFX_RESOLVE_NONE);
    std::uint8_t count = 1;
    TextureHandle depthHandle;
    if (info.depth) {
      const bgfx::TextureHandle depth =
          bgfx::createTexture2D(info.width, info.height, false, 1,
                                bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT);
      if (!bgfx::isValid(depth)) {
        destroy(color);
        error = "bgfx could not create render-target depth texture.";
        return {};
      }
      depthHandle = textures_.insert(depth.idx);
      attachments[1].init(depth, bgfx::Access::Write, 0, 1, 0,
                          BGFX_RESOLVE_NONE);
      count = 2;
    }
    const bgfx::FrameBufferHandle frameBuffer =
        bgfx::createFrameBuffer(count, attachments.data(), false);
    if (!bgfx::isValid(frameBuffer)) {
      if (depthHandle)
        destroy(depthHandle);
      destroy(color);
      error = "bgfx could not create framebuffer " + info.debugName + ".";
      return {};
    }
    return {.frameBuffer = frameBuffers_.insert(frameBuffer.idx),
            .color = color,
            .depth = depthHandle};
  }

  bool destroy(const TextureHandle handle) override {
    std::uint16_t value = Invalid;
    if (!textures_.remove(handle, &value))
      return false;
    bgfx::destroy(bgfx::TextureHandle{value});
    return true;
  }
  bool destroy(const SamplerHandle handle) override {
    std::uint16_t value = Invalid;
    if (!samplers_.remove(handle, &value))
      return false;
    bgfx::destroy(bgfx::UniformHandle{value});
    return true;
  }
  bool destroy(const BufferHandle handle) override {
    BufferValue value;
    if (!buffers_.remove(handle, &value))
      return false;
    if (value.kind == BufferKind::Vertex)
      bgfx::destroy(bgfx::VertexBufferHandle{value.index});
    else
      bgfx::destroy(bgfx::IndexBufferHandle{value.index});
    return true;
  }
  bool destroy(const ProgramHandle handle) override {
    std::uint16_t value = Invalid;
    if (!programs_.remove(handle, &value))
      return false;
    bgfx::destroy(bgfx::ProgramHandle{value});
    return true;
  }
  bool destroy(const FrameBufferHandle handle) override {
    std::uint16_t value = Invalid;
    if (!frameBuffers_.remove(handle, &value))
      return false;
    bgfx::destroy(bgfx::FrameBufferHandle{value});
    return true;
  }

  void clear() override {
    frameBuffers_.clear([](std::uint16_t value) {
      bgfx::destroy(bgfx::FrameBufferHandle{value});
    });
    programs_.clear(
        [](std::uint16_t value) { bgfx::destroy(bgfx::ProgramHandle{value}); });
    buffers_.clear([](const BufferValue &value) {
      if (value.kind == BufferKind::Vertex)
        bgfx::destroy(bgfx::VertexBufferHandle{value.index});
      else
        bgfx::destroy(bgfx::IndexBufferHandle{value.index});
    });
    samplers_.clear(
        [](std::uint16_t value) { bgfx::destroy(bgfx::UniformHandle{value}); });
    textures_.clear(
        [](std::uint16_t value) { bgfx::destroy(bgfx::TextureHandle{value}); });
  }

  bgfx::TextureHandle bgfxTexture(const TextureHandle handle) const override {
    const std::uint16_t *value = textures_.find(handle);
    return value != nullptr ? bgfx::TextureHandle{*value}
                            : bgfx::TextureHandle{Invalid};
  }

  bgfx::UniformHandle bgfxSampler(const SamplerHandle handle) const override {
    const std::uint16_t *value = samplers_.find(handle);
    return value != nullptr ? bgfx::UniformHandle{*value}
                            : bgfx::UniformHandle{Invalid};
  }

  bgfx::ProgramHandle bgfxProgram(const ProgramHandle handle) const override {
    const std::uint16_t *value = programs_.find(handle);
    return value != nullptr ? bgfx::ProgramHandle{*value}
                            : bgfx::ProgramHandle{Invalid};
  }

  bgfx::FrameBufferHandle
  bgfxFrameBuffer(const FrameBufferHandle handle) const override {
    const std::uint16_t *value = frameBuffers_.find(handle);
    return value != nullptr ? bgfx::FrameBufferHandle{*value}
                            : bgfx::FrameBufferHandle{Invalid};
  }

private:
  ResourceHandlePool<TextureTag, std::uint16_t> textures_;
  ResourceHandlePool<SamplerTag, std::uint16_t> samplers_;
  ResourceHandlePool<BufferTag, BufferValue> buffers_;
  ResourceHandlePool<ProgramTag, std::uint16_t> programs_;
  ResourceHandlePool<FrameBufferTag, std::uint16_t> frameBuffers_;
};

} // namespace

std::unique_ptr<GpuResources> createBgfxGpuResources() {
  return std::make_unique<BgfxGpuResources>();
}

} // namespace demi::runtime::render
