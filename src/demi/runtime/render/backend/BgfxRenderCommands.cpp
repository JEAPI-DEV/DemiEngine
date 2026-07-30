#include "demi/runtime/render/backend/RenderCommands.h"

#include "demi/runtime/render/backend/BgfxResourceLookup.h"
#include "demi/runtime/render/backend/BgfxVertexLayout.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <cstring>
namespace demi::runtime::render {
namespace {

std::uint64_t renderState(const BlendMode blend) {
  std::uint64_t state =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;
  switch (blend) {
  case BlendMode::Opaque: break;
  case BlendMode::Alpha:
    state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                   BGFX_STATE_BLEND_INV_SRC_ALPHA);
    break;
  case BlendMode::Additive:
    state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                   BGFX_STATE_BLEND_ONE);
    break;
  }
  return state;
}

class BgfxRenderCommands final : public RenderCommands {
public:
  explicit BgfxRenderCommands(BgfxResourceLookup &resources)
      : resources_(resources) {}

  bool configureView2D(const View2DConfig &view,
                       std::string &error) override {
    if (view.width == 0 || view.height == 0) {
      error = "2D view dimensions must be positive.";
      return false;
    }
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    if (view.frameBuffer) {
      frameBuffer = resources_.bgfxFrameBuffer(view.frameBuffer);
      if (!bgfx::isValid(frameBuffer)) {
        error = "2D view references a stale framebuffer handle.";
        return false;
      }
    }
    bgfx::setViewFrameBuffer(view.id, frameBuffer);
    bgfx::setViewRect(view.id, 0, 0, view.width, view.height);
    bgfx::setViewClear(view.id,
                       view.clear ? BGFX_CLEAR_COLOR : BGFX_CLEAR_NONE,
                       view.clearRgba, 1.0F, 0);
    float projection[16];
    const bgfx::Caps *caps = bgfx::getCaps();
    bx::mtxOrtho(projection, 0.0F, static_cast<float>(view.width),
                 static_cast<float>(view.height), 0.0F, -1.0F, 1.0F, 0.0F,
                 caps != nullptr && caps->homogeneousDepth);
    bgfx::setViewTransform(view.id, nullptr, projection);
    bgfx::touch(view.id);
    return true;
  }

  bool submit(const TransientDraw &draw, std::string &error) override {
    if (draw.vertices.empty() || draw.indices.empty()) {
      error = "Transient draws require vertices and indices.";
      return false;
    }
    bgfx::VertexLayout layout;
    if (!buildBgfxVertexLayout(draw.vertexLayout, layout, error))
      return false;
    if (draw.vertices.size() % layout.getStride() != 0) {
      error = "Transient vertex bytes must match the vertex layout stride.";
      return false;
    }
    const std::uint32_t vertexCount =
        static_cast<std::uint32_t>(draw.vertices.size() / layout.getStride());
    const std::uint32_t indexCount =
        static_cast<std::uint32_t>(draw.indices.size());
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, layout) <
            vertexCount ||
        bgfx::getAvailTransientIndexBuffer(indexCount) < indexCount) {
      error = "The frame transient-buffer budget is exhausted.";
      return false;
    }
    const bgfx::ProgramHandle program = resources_.bgfxProgram(draw.program);
    if (!bgfx::isValid(program)) {
      error = "Transient draw references a stale program handle.";
      return false;
    }
    const bgfx::TextureHandle texture = resources_.bgfxTexture(draw.texture);
    const bgfx::UniformHandle sampler = resources_.bgfxSampler(draw.sampler);
    if (!bgfx::isValid(texture) || !bgfx::isValid(sampler)) {
      error = "Transient draw references a stale texture or sampler handle.";
      return false;
    }

    bgfx::TransientVertexBuffer vertices;
    bgfx::TransientIndexBuffer indices;
    bgfx::allocTransientVertexBuffer(&vertices, vertexCount, layout);
    bgfx::allocTransientIndexBuffer(&indices, indexCount);
    std::memcpy(vertices.data, draw.vertices.data(), draw.vertices.size());
    std::memcpy(indices.data, draw.indices.data(),
                draw.indices.size_bytes());

    bgfx::setVertexBuffer(0, &vertices);
    bgfx::setIndexBuffer(&indices);
    bgfx::setTexture(0, sampler, texture);
    bgfx::setState(renderState(draw.blend));
    if (draw.scissor.width > 0 && draw.scissor.height > 0)
      bgfx::setScissor(draw.scissor.x, draw.scissor.y, draw.scissor.width,
                       draw.scissor.height);
    bgfx::submit(draw.viewId, program);
    return true;
  }

private:
  BgfxResourceLookup &resources_;
};

} // namespace

std::unique_ptr<RenderCommands>
createBgfxRenderCommands(GpuResources &resources) {
  auto *lookup = dynamic_cast<BgfxResourceLookup *>(&resources);
  return lookup != nullptr ? std::make_unique<BgfxRenderCommands>(*lookup)
                           : nullptr;
}

} // namespace demi::runtime::render
