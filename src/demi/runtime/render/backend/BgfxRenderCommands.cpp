#include "demi/runtime/render/backend/RenderCommands.h"

#include "demi/runtime/render/backend/BgfxResourceLookup.h"
#include "demi/runtime/render/backend/BgfxVertexLayout.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <cstring>
namespace demi::runtime::render {
namespace {

std::uint64_t renderState(const DrawState &draw) {
  std::uint64_t state = BGFX_STATE_NONE;
  if (draw.writeColor)
    state |= BGFX_STATE_WRITE_RGB;
  if (draw.writeAlpha)
    state |= BGFX_STATE_WRITE_A;
  if (draw.writeDepth)
    state |= BGFX_STATE_WRITE_Z;
  if (draw.multisampling)
    state |= BGFX_STATE_MSAA;
  switch (draw.blend) {
  case BlendMode::Opaque:
    break;
  case BlendMode::Alpha:
    state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                   BGFX_STATE_BLEND_INV_SRC_ALPHA);
    break;
  case BlendMode::Additive:
    state |=
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
    break;
  }
  switch (draw.depthTest) {
  case DepthTest::Disabled:
    break;
  case DepthTest::Less:
    state |= BGFX_STATE_DEPTH_TEST_LESS;
    break;
  case DepthTest::LessEqual:
    state |= BGFX_STATE_DEPTH_TEST_LEQUAL;
    break;
  case DepthTest::Always:
    state |= BGFX_STATE_DEPTH_TEST_ALWAYS;
    break;
  }
  switch (draw.cull) {
  case CullMode::None:
    break;
  case CullMode::Clockwise:
    state |= BGFX_STATE_CULL_CW;
    break;
  case CullMode::CounterClockwise:
    state |= BGFX_STATE_CULL_CCW;
    break;
  }
  switch (draw.topology) {
  case PrimitiveTopology::Triangles:
    break;
  case PrimitiveTopology::Lines:
    state |= BGFX_STATE_PT_LINES;
    break;
  case PrimitiveTopology::Points:
    state |= BGFX_STATE_PT_POINTS;
    break;
  }
  return state;
}

bool positiveView(const std::uint16_t width, const std::uint16_t height,
                  std::string &error, const char *name) {
  if (width > 0 && height > 0)
    return true;
  error = std::string(name) + " view dimensions must be positive.";
  return false;
}

bool setFrameBuffer(const std::uint16_t viewId,
                    const FrameBufferHandle frameBuffer,
                    BgfxResourceLookup &resources, std::string &error,
                    const char *name) {
  bgfx::FrameBufferHandle native = BGFX_INVALID_HANDLE;
  if (frameBuffer) {
    native = resources.bgfxFrameBuffer(frameBuffer);
    if (!bgfx::isValid(native)) {
      error =
          std::string(name) + " view references a stale framebuffer handle.";
      return false;
    }
  }
  bgfx::setViewFrameBuffer(viewId, native);
  return true;
}

bool bindProgramAndTexture(const ProgramHandle programHandle,
                           const TextureHandle textureHandle,
                           const SamplerHandle samplerHandle,
                           BgfxResourceLookup &resources,
                           bgfx::ProgramHandle &program, std::string &error,
                           const char *drawName) {
  program = resources.bgfxProgram(programHandle);
  if (!bgfx::isValid(program)) {
    error = std::string(drawName) + " references a stale program handle.";
    return false;
  }
  if (static_cast<bool>(textureHandle) != static_cast<bool>(samplerHandle)) {
    error = std::string(drawName) +
            " must provide both texture and sampler or neither.";
    return false;
  }
  if (!textureHandle)
    return true;
  const bgfx::TextureHandle texture = resources.bgfxTexture(textureHandle);
  const bgfx::UniformHandle sampler = resources.bgfxSampler(samplerHandle);
  if (!bgfx::isValid(texture) || !bgfx::isValid(sampler)) {
    error = std::string(drawName) +
            " references a stale texture or sampler handle.";
    return false;
  }
  bgfx::setTexture(0, sampler, texture);
  return true;
}

void applyScissor(const ScissorRect scissor) {
  if (scissor.width > 0 && scissor.height > 0)
    bgfx::setScissor(scissor.x, scissor.y, scissor.width, scissor.height);
}

bool validateUniforms(const std::span<const DrawUniformValue> uniforms,
                      BgfxResourceLookup &resources, std::string &error,
                      const char *drawName) {
  for (const DrawUniformValue &value : uniforms) {
    const bgfx::UniformHandle uniform = resources.bgfxUniform(value.handle);
    if (!bgfx::isValid(uniform) || value.values.empty() || value.count == 0) {
      error = std::string(drawName) + " references an invalid uniform value.";
      return false;
    }
  }
  return true;
}

bool validateBufferedSlices(const BufferSlice vertices,
                            const BufferSlice indices,
                            BgfxResourceLookup &resources,
                            BgfxBufferReference &nativeVertices,
                            BgfxBufferReference &nativeIndices,
                            std::string &error, const char *drawName) {
  nativeVertices = resources.bgfxBuffer(vertices.handle);
  nativeIndices = resources.bgfxBuffer(indices.handle);
  if (nativeVertices.index == UINT16_MAX ||
      nativeVertices.kind != BufferKind::Vertex) {
    error = std::string(drawName) + " references a stale or non-vertex buffer.";
    return false;
  }
  if (nativeIndices.index == UINT16_MAX ||
      (nativeIndices.kind != BufferKind::Index16 &&
       nativeIndices.kind != BufferKind::Index32)) {
    error = std::string(drawName) + " references a stale or non-index buffer.";
    return false;
  }
  if (vertices.count == 0 || indices.count == 0) {
    error = std::string(drawName) + " slices must not be empty.";
    return false;
  }
  return true;
}

void applyUniforms(const std::span<const DrawUniformValue> uniforms,
                   BgfxResourceLookup &resources) {
  for (const DrawUniformValue &value : uniforms)
    bgfx::setUniform(resources.bgfxUniform(value.handle), value.values.data(),
                     value.count);
}

class BgfxRenderCommands final : public RenderCommands {
public:
  explicit BgfxRenderCommands(BgfxResourceLookup &resources)
      : resources_(resources) {}

  bool configureView2D(const View2DConfig &view, std::string &error) override {
    if (!positiveView(view.width, view.height, error, "2D"))
      return false;
    if (!setFrameBuffer(view.id, view.frameBuffer, resources_, error, "2D"))
      return false;
    bgfx::setViewRect(view.id, view.x, view.y, view.width, view.height);
    // Canvas draw order is presentation order: tilemap layers, sprites, debug
    // geometry, and UI may deliberately overlap while sharing no depth buffer.
    // bgfx's default state sorting is valid for opaque 3D work, but can move a
    // background quad in front of later 2D submissions.
    bgfx::setViewMode(view.id, bgfx::ViewMode::Sequential);
    bgfx::setViewClear(view.id, view.clear ? BGFX_CLEAR_COLOR : BGFX_CLEAR_NONE,
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

  bool configureView3D(const View3DConfig &view, std::string &error) override {
    if (!positiveView(view.width, view.height, error, "3D"))
      return false;
    if (!(view.nearClip > 0.0F) || !(view.farClip > view.nearClip)) {
      error = "3D view clipping planes must satisfy 0 < near < far.";
      return false;
    }
    if (view.perspective &&
        !(view.verticalFovDegrees > 0.0F && view.verticalFovDegrees < 180.0F)) {
      error = "Perspective field of view must be between 0 and 180 degrees.";
      return false;
    }
    if (!view.perspective && !(view.orthographicSize > 0.0F)) {
      error = "Orthographic size must be positive.";
      return false;
    }
    if (!setFrameBuffer(view.id, view.frameBuffer, resources_, error, "3D"))
      return false;

    bgfx::setViewRect(view.id, view.x, view.y, view.width, view.height);
    std::uint16_t clearFlags = BGFX_CLEAR_NONE;
    if (view.clearColor)
      clearFlags |= BGFX_CLEAR_COLOR;
    if (view.clearDepth)
      clearFlags |= BGFX_CLEAR_DEPTH;
    bgfx::setViewClear(view.id, clearFlags, view.clearRgba, 1.0F, 0);

    const bx::Vec3 eye{view.eye.x, view.eye.y, view.eye.z};
    const bx::Vec3 target{view.target.x, view.target.y, view.target.z};
    const bx::Vec3 up{view.up.x, view.up.y, view.up.z};
    float viewMatrix[16];
    float projection[16];
    bx::mtxLookAt(viewMatrix, eye, target, up, bx::Handedness::Right);
    const bgfx::Caps *caps = bgfx::getCaps();
    const bool homogeneousDepth = caps != nullptr && caps->homogeneousDepth;
    const float aspect =
        static_cast<float>(view.width) / static_cast<float>(view.height);
    if (view.perspective)
      bx::mtxProj(projection, view.verticalFovDegrees, aspect, view.nearClip,
                  view.farClip, homogeneousDepth, bx::Handedness::Right);
    else {
      const float halfHeight = view.orthographicSize * 0.5F;
      const float halfWidth = halfHeight * aspect;
      bx::mtxOrtho(projection, -halfWidth, halfWidth, -halfHeight, halfHeight,
                   view.nearClip, view.farClip, 0.0F, homogeneousDepth,
                   bx::Handedness::Right);
    }
    bgfx::setViewTransform(view.id, viewMatrix, projection);
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
    if (!validateUniforms(draw.uniforms, resources_, error, "Transient draw"))
      return false;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    if (!bindProgramAndTexture(draw.program, draw.texture, draw.sampler,
                               resources_, program, error, "Transient draw"))
      return false;

    bgfx::TransientVertexBuffer vertices;
    bgfx::TransientIndexBuffer indices;
    bgfx::allocTransientVertexBuffer(&vertices, vertexCount, layout);
    bgfx::allocTransientIndexBuffer(&indices, indexCount);
    std::memcpy(vertices.data, draw.vertices.data(), draw.vertices.size());
    std::memcpy(indices.data, draw.indices.data(), draw.indices.size_bytes());

    bgfx::setVertexBuffer(0, &vertices);
    bgfx::setIndexBuffer(&indices);
    applyUniforms(draw.uniforms, resources_);
    DrawState state = draw.state;
    state.blend = draw.blend;
    bgfx::setState(renderState(state));
    applyScissor(draw.scissor);
    bgfx::submit(draw.viewId, program);
    return true;
  }

  bool submit(const BufferedDraw &draw, std::string &error) override {
    BgfxBufferReference vertices;
    BgfxBufferReference indices;
    if (!validateBufferedSlices(draw.vertices, draw.indices, resources_,
                                vertices, indices, error, "Buffered draw"))
      return false;
    if (!validateUniforms(draw.uniforms, resources_, error, "Buffered draw"))
      return false;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    if (!bindProgramAndTexture(draw.program, draw.texture, draw.sampler,
                               resources_, program, error, "Buffered draw"))
      return false;

    bgfx::setVertexBuffer(0, bgfx::VertexBufferHandle{vertices.index},
                          draw.vertices.first, draw.vertices.count);
    bgfx::setIndexBuffer(bgfx::IndexBufferHandle{indices.index},
                         draw.indices.first, draw.indices.count);
    bgfx::setTransform(draw.transform.data());
    applyUniforms(draw.uniforms, resources_);
    bgfx::setState(renderState(draw.state));
    applyScissor(draw.scissor);
    bgfx::submit(draw.viewId, program);
    return true;
  }

  bool submit(const InstancedBufferedDraw &draw, std::string &error) override {
    BgfxBufferReference vertices;
    BgfxBufferReference indices;
    if (!validateBufferedSlices(draw.vertices, draw.indices, resources_,
                                vertices, indices, error, "Instanced draw"))
      return false;
    if (draw.transforms.empty()) {
      error = "Instanced draws require at least one transform.";
      return false;
    }
    if (draw.transforms.size() > UINT16_MAX) {
      error = "Instanced draw count exceeds the 16-bit backend limit.";
      return false;
    }
    const std::uint16_t instanceCount =
        static_cast<std::uint16_t>(draw.transforms.size());
    constexpr std::uint16_t TransformStride = sizeof(float) * 16U;
    if (bgfx::getAvailInstanceDataBuffer(instanceCount, TransformStride) <
        instanceCount) {
      error = "The frame instance-buffer budget is exhausted.";
      return false;
    }
    if (!validateUniforms(draw.uniforms, resources_, error, "Instanced draw"))
      return false;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    if (!bindProgramAndTexture(draw.program, draw.texture, draw.sampler,
                               resources_, program, error, "Instanced draw"))
      return false;

    bgfx::InstanceDataBuffer instances;
    bgfx::allocInstanceDataBuffer(&instances, instanceCount, TransformStride);
    std::memcpy(instances.data, draw.transforms.data(),
                draw.transforms.size_bytes());
    bgfx::setVertexBuffer(0, bgfx::VertexBufferHandle{vertices.index},
                          draw.vertices.first, draw.vertices.count);
    bgfx::setIndexBuffer(bgfx::IndexBufferHandle{indices.index},
                         draw.indices.first, draw.indices.count);
    bgfx::setInstanceDataBuffer(&instances);
    applyUniforms(draw.uniforms, resources_);
    bgfx::setState(renderState(draw.state));
    applyScissor(draw.scissor);
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
