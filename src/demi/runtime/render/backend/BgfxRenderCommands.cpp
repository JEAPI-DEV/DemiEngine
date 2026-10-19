#include "demi/runtime/render/backend/RenderCommands.h"

#include "demi/runtime/render/backend/BgfxDrawBindings.h"
#include "demi/runtime/render/backend/BgfxInstanceBufferPool.h"
#include "demi/runtime/render/backend/BgfxFrameSerial.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/backend/BgfxResourceLookup.h"
#include "demi/runtime/render/backend/BgfxVertexLayout.h"
#include "demi/runtime/render/backend/BgfxView3D.h"

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

struct ProgramTextureBinding {
  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
};

bool resolveProgramAndTexture(const ProgramHandle programHandle,
                              const TextureHandle textureHandle,
                              const SamplerHandle samplerHandle,
                              BgfxResourceLookup &resources,
                              ProgramTextureBinding &binding,
                              std::string &error, const char *drawName) {
  binding.program = resources.bgfxProgram(programHandle);
  if (!bgfx::isValid(binding.program)) {
    error = std::string(drawName) + " references a stale program handle.";
    return false;
  }
  if (static_cast<bool>(textureHandle) != static_cast<bool>(samplerHandle)) {
    error = std::string(drawName) +
            " must provide both texture and sampler or neither.";
    return false;
  }
  if (!textureHandle) {
    return true;
  }
  binding.texture = resources.bgfxTexture(textureHandle);
  binding.sampler = resources.bgfxSampler(samplerHandle);
  if (!bgfx::isValid(binding.texture) || !bgfx::isValid(binding.sampler)) {
    error = std::string(drawName) +
            " references a stale texture or sampler handle.";
    return false;
  }
  return true;
}

void bindPrimaryTexture(const ProgramTextureBinding &binding) {
  if (bgfx::isValid(binding.texture)) {
    bgfx::setTexture(0, binding.sampler, binding.texture);
  }
}

bool bindProgramAndTexture(const ProgramHandle programHandle,
                           const TextureHandle textureHandle,
                           const SamplerHandle samplerHandle,
                           BgfxResourceLookup &resources,
                           bgfx::ProgramHandle &program, std::string &error,
                           const char *drawName) {
  ProgramTextureBinding binding;
  if (!resolveProgramAndTexture(programHandle, textureHandle, samplerHandle,
                                 resources, binding, error, drawName)) {
    return false;
  }
  program = binding.program;
  bindPrimaryTexture(binding);
  return true;
}

void applyScissor(const ScissorRect scissor) {
  if (scissor.width > 0 && scissor.height > 0)
    bgfx::setScissor(scissor.x, scissor.y, scissor.width, scissor.height);
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
      (nativeVertices.kind != BufferKind::Vertex &&
       nativeVertices.kind != BufferKind::DynamicVertex)) {
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

void applyUniforms(std::span<const BgfxResolvedUniform> uniforms) {
  for (const auto &uniform : uniforms) {
    bgfx::setUniform(uniform.handle, uniform.values.data(), uniform.count);
  }
}

void bindExtraTextures(std::span<const BgfxResolvedTexture> textures) {
  for (const auto &texture : textures) {
    bgfx::setTexture(texture.stage, texture.sampler, texture.texture);
  }
}

class BgfxRenderCommands final : public RenderCommands {
public:
  BgfxRenderCommands(BgfxResourceLookup &resources,
                      const BgfxRenderCommandsOptions &options)
      : resources_(resources), instanceBuffers_(options) {}

  std::uint32_t availableInstances(std::uint16_t stride,
                                   std::uint32_t requested) const override {
    if (!bgfxSubmissionContextActive()) {
      return 0;
    }
    const auto *caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_INSTANCING) == 0 ||
        stride == 0 || stride % 16 != 0 || requested == 0) {
      return 0;
    }
    return instanceBuffers_.chunkCapacity(stride, requested);
  }

  bool copyTexture(std::uint16_t view, TextureHandle source,
                   TextureHandle destination, std::uint16_t width,
                   std::uint16_t height, std::string &error) override {
    const auto sourceTexture = resources_.bgfxTexture(source);
    const auto destinationTexture = resources_.bgfxTexture(destination);
    const auto *caps = bgfx::getCaps();
    if (!caps || view >= caps->limits.maxViews ||
        !(caps->supported & BGFX_CAPS_TEXTURE_BLIT) ||
        !bgfx::isValid(sourceTexture) || !bgfx::isValid(destinationTexture) ||
        source == destination || width == 0 || height == 0) {
      error = "Scene capture requires distinct valid textures and texture blit "
              "support.";
      return false;
    }
    bgfx::setViewName(view, "OpaqueColorCapture");
    bgfx::blit(view, destinationTexture, 0, 0, sourceTexture, 0, 0, width,
               height);
    return true;
  }

  bool configureView2D(const View2DConfig &view, std::string &error) override {
    if (!positiveView(view.width, view.height, error, "2D"))
      return false;
    if (!setFrameBuffer(view.id, view.frameBuffer, resources_, error, "2D"))
      return false;
    const auto name =
        view.debugName.empty() ? std::string_view{"2D"} : view.debugName;
    bgfx::setViewName(view.id, name.data(),
                      static_cast<std::int32_t>(name.size()));
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

    const auto name =
        view.debugName.empty() ? std::string_view{"3D"} : view.debugName;
    bgfx::setViewName(view.id, name.data(),
                      static_cast<std::int32_t>(name.size()));
    bgfx::setViewRect(view.id, view.x, view.y, view.width, view.height);
    std::uint16_t clearFlags = BGFX_CLEAR_NONE;
    bgfx::setViewMode(view.id, view.sequential ? bgfx::ViewMode::Sequential
                                               : bgfx::ViewMode::Default);
    if (view.clearColor)
      clearFlags |= BGFX_CLEAR_COLOR;
    if (view.clearDepth)
      clearFlags |= BGFX_CLEAR_DEPTH;
    bgfx::setViewClear(view.id, clearFlags, view.clearRgba, 1.0F, 0);

    const auto viewMatrix = bgfxViewMatrix3D(view);
    float projection[16];
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
    bgfx::setViewTransform(view.id, viewMatrix.data(), projection);
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
    if (!bindings_.resolveUniforms(draw.uniforms, resources_, error,
                                   "Transient draw")) {
      return false;
    }
    const auto *caps = bgfx::getCaps();
    const std::uint16_t maximumSamplers =
        caps ? caps->limits.maxTextureSamplers : 256;
    if (!bindings_.resolveTextures(draw.textures, resources_, maximumSamplers,
                                   error)) {
      return false;
    }
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    if (!bindProgramAndTexture(draw.program, draw.texture, draw.sampler,
                               resources_, program, error, "Transient draw")) {
      return false;
    }
    bindExtraTextures(bindings_.textures());

    bgfx::TransientVertexBuffer vertices;
    bgfx::TransientIndexBuffer indices;
    bgfx::allocTransientVertexBuffer(&vertices, vertexCount, layout);
    bgfx::allocTransientIndexBuffer(&indices, indexCount);
    std::memcpy(vertices.data, draw.vertices.data(), draw.vertices.size());
    std::memcpy(indices.data, draw.indices.data(), draw.indices.size_bytes());

    bgfx::setVertexBuffer(0, &vertices);
    bgfx::setIndexBuffer(&indices);
    applyUniforms(bindings_.uniforms());
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
    if (!bindings_.resolveUniforms(draw.uniforms, resources_, error,
                                   "Buffered draw")) {
      return false;
    }
    const auto *caps = bgfx::getCaps();
    const std::uint16_t maximumSamplers =
        caps ? caps->limits.maxTextureSamplers : 256;
    if (!bindings_.resolveTextures(draw.textures, resources_, maximumSamplers,
                                   error)) {
      return false;
    }
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    if (!bindProgramAndTexture(draw.program, draw.texture, draw.sampler,
                               resources_, program, error, "Buffered draw")) {
      return false;
    }
    bindExtraTextures(bindings_.textures());

    if (vertices.kind == BufferKind::DynamicVertex)
      bgfx::setVertexBuffer(0, bgfx::DynamicVertexBufferHandle{vertices.index},
                            draw.vertices.first, draw.vertices.count);
    else
      bgfx::setVertexBuffer(0, bgfx::VertexBufferHandle{vertices.index},
                            draw.vertices.first, draw.vertices.count);
    bgfx::setIndexBuffer(bgfx::IndexBufferHandle{indices.index},
                         draw.indices.first, draw.indices.count);
    bgfx::setTransform(draw.transform.data());
    applyUniforms(bindings_.uniforms());
    bgfx::setState(renderState(draw.state));
    applyScissor(draw.scissor);
    bgfx::submit(draw.viewId, program);
    return true;
  }

  bool submit(const InstancedBufferedDraw &draw, std::string &error) override {
    if (!bgfxSubmissionContextActive()) {
      error = "Instanced submission requires an active native graphics context.";
      return false;
    }
    BgfxBufferReference vertices;
    BgfxBufferReference indices;
    if (!validateBufferedSlices(draw.vertices, draw.indices, resources_,
                                vertices, indices, error, "Instanced draw"))
      return false;
    const auto stride = draw.instances.stride;
    if (draw.instances.data.empty()) {
      error = "Instanced draws require at least one instance record.";
      return false;
    }
    if (stride == 0 || stride % 16 != 0 ||
        draw.instances.data.size() % stride != 0) {
      error = "Instance records require a sixteen-byte-aligned stride and "
              "complete payload.";
      return false;
    }
    const auto count = draw.instances.data.size() / stride;
    if (count > UINT32_MAX) {
      error = "Instance count exceeds the native 32-bit buffer limit.";
      return false;
    }
    const auto instanceCount = static_cast<std::uint32_t>(count);
    const auto *instanceCaps = bgfx::getCaps();
    if (!instanceCaps || (instanceCaps->supported & BGFX_CAPS_INSTANCING) == 0) {
      error = "Instancing is unsupported on this graphics device.";
      return false;
    }
    if (instanceBuffers_.remainingRecords(stride) < instanceCount) {
      error = instanceBuffers_.budgetError(stride, instanceCount);
      return false;
    }
    if (!bindings_.resolveUniforms(draw.uniforms, resources_, error,
                                   "Instanced draw")) {
      return false;
    }
    const auto *caps = bgfx::getCaps();
    const std::uint16_t maximumSamplers =
        caps ? caps->limits.maxTextureSamplers : 256;
    if (!bindings_.resolveTextures(draw.textures, resources_, maximumSamplers,
                                   error)) {
      return false;
    }
    ProgramTextureBinding primary;
    if (!resolveProgramAndTexture(draw.program, draw.texture, draw.sampler,
                                   resources_, primary, error, "Instanced draw")) {
      return false;
    }
    std::uint32_t first = 0;
    while (first < instanceCount) {
      const auto count = instanceBuffers_.chunkCapacity(stride, instanceCount - first);
      if (count == 0) {
        error = "Dedicated instance-buffer capacity changed during submission.";
        return false;
      }
      BgfxInstanceBufferPool::Allocation instances;
      const auto bytes = draw.instances.data.subspan(std::size_t(first) * stride,
                                                     std::size_t(count) * stride);
      if (!instanceBuffers_.upload(stride, bytes, instances, error)) {
        return false;
      }
      // submit discards state. Every bounded chunk rebinds the complete surface
      // and uses its own arena range; later passes cannot overwrite it.
      bindPrimaryTexture(primary);
      bindExtraTextures(bindings_.textures());
      if (vertices.kind == BufferKind::DynamicVertex) {
        bgfx::setVertexBuffer(0, bgfx::DynamicVertexBufferHandle{vertices.index},
                              draw.vertices.first, draw.vertices.count);
      } else {
        bgfx::setVertexBuffer(0, bgfx::VertexBufferHandle{vertices.index},
                              draw.vertices.first, draw.vertices.count);
      }
      bgfx::setIndexBuffer(bgfx::IndexBufferHandle{indices.index},
                           draw.indices.first, draw.indices.count);
      bgfx::setInstanceDataBuffer(instances.buffer, instances.first, instances.count);
      applyUniforms(bindings_.uniforms());
      bgfx::setState(renderState(draw.state));
      applyScissor(draw.scissor);
      bgfx::submit(draw.viewId, primary.program);
      first += count;
    }
    if (RuntimeProfiler::enabled()) {
      RuntimeProfiler::setGauge("Graphics.instance_buffer_bytes", instanceBuffers_.residentBytes());
      RuntimeProfiler::setGauge("Graphics.instance_upload_bytes", instanceBuffers_.frameUploadBytes());
      RuntimeProfiler::setGauge("Graphics.instance_chunks", instanceBuffers_.frameChunks());
    }
    return true;
  }

private:
  BgfxResourceLookup &resources_;
  BgfxDrawBindings bindings_;
  BgfxInstanceBufferPool instanceBuffers_;
};

} // namespace

std::array<float, 16> bgfxViewMatrix3D(const View3DConfig &view) {
  std::array<float, 16> matrix;
  const bx::Vec3 origin{0, 0, 0};
  const bx::Vec3 forward{view.forward.x, view.forward.y, view.forward.z};
  const bx::Vec3 up{view.up.x, view.up.y, view.up.z};
  bx::mtxLookAt(matrix.data(), origin, forward, up, bx::Handedness::Right);
  for (std::size_t row = 0; row < 3; ++row) {
    const double translation = double(matrix[row]) * view.eye.x +
                               double(matrix[4 + row]) * view.eye.y +
                               double(matrix[8 + row]) * view.eye.z;
    matrix[12 + row] = float(-translation);
  }
  return matrix;
}

std::unique_ptr<RenderCommands>
createBgfxRenderCommands(GpuResources &resources,
                         const BgfxRenderCommandsOptions &options,
                         std::string *error) {
  if (!options.valid()) {
    if (error) {
      *error = "Invalid BgfxRenderCommandsOptions: instance chunks must be aligned "
               "to 16 bytes, fit the native 32-bit upload limit, and divide the "
               "nonzero reservation budget.";
    }
    return nullptr;
  }
  auto *lookup = dynamic_cast<BgfxResourceLookup *>(&resources);
  if (!lookup) {
    if (error) {
      *error = "Native render commands require a bgfx resource owner.";
    }
    return nullptr;
  }
  return std::make_unique<BgfxRenderCommands>(*lookup, options);
}

} // namespace demi::runtime::render
