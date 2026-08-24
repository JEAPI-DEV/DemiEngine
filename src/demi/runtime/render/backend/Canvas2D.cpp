#include "demi/runtime/render/backend/Canvas2D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

namespace demi::runtime::render {
namespace {

VertexLayout quadVertexLayout() {
  return VertexLayout{.attributes = {
                          {.semantic = VertexSemantic::Position,
                           .components = 2,
                           .type = VertexElementType::Float},
                          {.semantic = VertexSemantic::TexCoord0,
                           .components = 2,
                           .type = VertexElementType::Float},
                          {.semantic = VertexSemantic::Color0,
                           .components = 4,
                           .type = VertexElementType::UInt8,
                           .normalized = true},
                      }};
}

bool positive(const Rect2D &rect) {
  return rect.width > 0.0F && rect.height > 0.0F;
}

} // namespace

ScissorRect canvasScissorForView(ScissorRect local,
                                 const std::uint16_t viewportX,
                                 const std::uint16_t viewportY) {
  if (local.width == 0 || local.height == 0)
    return local;
  local.x = static_cast<std::uint16_t>(std::min<std::uint32_t>(
      static_cast<std::uint32_t>(local.x) + viewportX, UINT16_MAX));
  local.y = static_cast<std::uint16_t>(std::min<std::uint32_t>(
      static_cast<std::uint32_t>(local.y) + viewportY, UINT16_MAX));
  return local;
}

Canvas2D::Canvas2D(GpuResources &resources, RenderCommands &commands,
                   const std::size_t maxQuadsPerDraw)
    : resources_(resources), commands_(commands), batch_(maxQuadsPerDraw) {}

Canvas2D::~Canvas2D() { shutdown(); }

bool Canvas2D::initialize(std::string &error) {
  if (whiteTexture_ && sampler_ && program_)
    return true;
  shutdown();
  constexpr std::array<std::byte, 4> White = {std::byte{0xff}, std::byte{0xff},
                                              std::byte{0xff}, std::byte{0xff}};
  whiteTexture_ = resources_.createTexture(
      TextureCreateInfo{.data = White, .debugName = "Canvas2D white"}, error);
  if (!whiteTexture_)
    return false;
  sampler_ = resources_.createSampler("s_tex", error);
  if (!sampler_) {
    shutdown();
    return false;
  }
  program_ = resources_.createBuiltinProgram(BuiltinProgram::Textured2D, error);
  if (!program_) {
    shutdown();
    return false;
  }
  return true;
}

void Canvas2D::shutdown() {
  batch_.clear();
  uniformSets_.clear();
  if (program_)
    resources_.destroy(program_);
  if (sampler_)
    resources_.destroy(sampler_);
  if (whiteTexture_)
    resources_.destroy(whiteTexture_);
  program_ = {};
  sampler_ = {};
  whiteTexture_ = {};
  statistics_ = {};
}

bool Canvas2D::begin(const std::uint16_t viewId, const std::uint16_t width,
                     const std::uint16_t height, const std::uint32_t clearRgba,
                     std::string &error, const bool clear,
                     const std::uint16_t x, const std::uint16_t y,
                     const FrameBufferHandle frameBuffer) {
  if (!whiteTexture_ || !sampler_ || !program_) {
    error = "Canvas2D must be initialized before beginning a frame.";
    return false;
  }
  batch_.clear();
  uniformSets_.clear();
  statistics_ = {};
  viewId_ = viewId;
  // Canvas callers author clip rectangles in viewport-local coordinates.
  // bgfx scissor rectangles are backbuffer coordinates for an embedded view.
  scissorOffsetX_ = frameBuffer ? 0 : x;
  scissorOffsetY_ = frameBuffer ? 0 : y;
  return commands_.configureView2D(View2DConfig{.id = viewId,
                                                .x = x,
                                                .y = y,
                                                .width = width,
                                                .height = height,
                                                .clearRgba = clearRgba,
                                                .clear = clear,
                                                .frameBuffer = frameBuffer},
                                   error);
}

bool Canvas2D::solid(const Rect2D &destination, const std::uint32_t rgba,
                     const BlendMode blend, const ScissorRect scissor,
                     const ProgramHandle program,
                     const std::uint32_t uniformSet) {
  return add(whiteTexture_, destination, {}, rgba, blend, scissor, program,
             uniformSet);
}

bool Canvas2D::image(const TextureHandle texture, const Rect2D &destination,
                     const TextureRegion2D &source, const std::uint32_t rgba,
                     const BlendMode blend, const ScissorRect scissor,
                     const ProgramHandle program,
                     const std::uint32_t uniformSet) {
  return add(texture, destination, source, rgba, blend, scissor, program,
             uniformSet);
}

bool Canvas2D::imageTransformed(
    const TextureHandle texture, const float positionX, const float positionY,
    const float width, const float height, const float pivotX,
    const float pivotY, const float rotationRadians,
    const TextureRegion2D &source, const std::uint32_t rgba,
    const BlendMode blend, const ScissorRect scissor, ProgramHandle program,
    const std::uint32_t uniformSet) {
  if (!program)
    program = program_;
  if (!texture || !program || width <= 0.0F || height <= 0.0F ||
      !std::isfinite(rotationRadians))
    return false;
  const float cosine = std::cos(rotationRadians);
  const float sine = std::sin(rotationRadians);
  const auto point = [&](const float localX, const float localY, const float u,
                         const float v) {
    return QuadVertex{
        .x = positionX + localX * cosine - localY * sine,
        .y = positionY + localX * sine + localY * cosine,
        .u = u,
        .v = v,
        .rgba = rgba,
    };
  };
  const float left = -pivotX * width;
  const float top = -pivotY * height;
  const float right = left + width;
  const float bottom = top + height;
  return batch_.addQuad(
      QuadBatchKey{.texture = texture,
                   .program = program,
                   .uniformSet = uniformSet,
                   .blend = blend,
                   .scissor = scissor},
      QuadCorners2D{
          .topLeft = point(left, top, source.u0, source.v0),
          .topRight = point(right, top, source.u1, source.v0),
          .bottomRight = point(right, bottom, source.u1, source.v1),
          .bottomLeft = point(left, bottom, source.u0, source.v1),
      });
}

bool Canvas2D::ninePatch(const TextureHandle texture, const Rect2D &destination,
                         const TextureRegion2D &source,
                         const NinePatch2D &border, const std::uint32_t rgba,
                         const BlendMode blend, const ScissorRect scissor,
                         const ProgramHandle program,
                         const std::uint32_t uniformSet) {
  if (!texture || !positive(destination) || source.u1 <= source.u0 ||
      source.v1 <= source.v0 || border.left < 0.0F || border.top < 0.0F ||
      border.right < 0.0F || border.bottom < 0.0F)
    return false;

  const float horizontalScale = std::min(
      1.0F, destination.width / std::max(border.left + border.right, 0.0001F));
  const float verticalScale = std::min(
      1.0F, destination.height / std::max(border.top + border.bottom, 0.0001F));
  const float xs[4] = {
      destination.x,
      destination.x + border.left * horizontalScale,
      destination.x + destination.width - border.right * horizontalScale,
      destination.x + destination.width,
  };
  const float ys[4] = {
      destination.y,
      destination.y + border.top * verticalScale,
      destination.y + destination.height - border.bottom * verticalScale,
      destination.y + destination.height,
  };
  const float us[4] = {
      source.u0,
      border.center.u0,
      border.center.u1,
      source.u1,
  };
  const float vs[4] = {
      source.v0,
      border.center.v0,
      border.center.v1,
      source.v1,
  };
  if (us[1] < source.u0 || us[2] > source.u1 || us[1] > us[2] ||
      vs[1] < source.v0 || vs[2] > source.v1 || vs[1] > vs[2])
    return false;

  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 3; ++x) {
      if (!add(texture,
               Rect2D{.x = xs[x],
                      .y = ys[y],
                      .width = xs[x + 1] - xs[x],
                      .height = ys[y + 1] - ys[y]},
               TextureRegion2D{
                   .u0 = us[x], .v0 = vs[y], .u1 = us[x + 1], .v1 = vs[y + 1]},
               rgba, blend, scissor, program, uniformSet))
        return false;
    }
  }
  return true;
}

bool Canvas2D::circle(const float centerX, const float centerY,
                      const float radius, const std::uint32_t rgba,
                      const int segments, const BlendMode blend,
                      const ScissorRect scissor, ProgramHandle program,
                      const std::uint32_t uniformSet) {
  if (!whiteTexture_ || radius <= 0.0F || segments < 3 || segments > 512)
    return false;
  if (!program)
    program = program_;
  const QuadBatchKey key{.texture = whiteTexture_,
                         .program = program,
                         .uniformSet = uniformSet,
                         .blend = blend,
                         .scissor = scissor};
  for (int index = 0; index < segments; ++index) {
    const float angle0 = 2.0F * std::numbers::pi_v<float> * index / segments;
    const float angle1 =
        2.0F * std::numbers::pi_v<float> * (index + 1) / segments;
    if (!batch_.addTriangle(key,
                            Triangle2D{
                                .a = {.x = centerX,
                                      .y = centerY,
                                      .u = 0.5F,
                                      .v = 0.5F,
                                      .rgba = rgba},
                                .b = {.x = centerX + std::cos(angle0) * radius,
                                      .y = centerY + std::sin(angle0) * radius,
                                      .u = 0.5F,
                                      .v = 0.5F,
                                      .rgba = rgba},
                                .c = {.x = centerX + std::cos(angle1) * radius,
                                      .y = centerY + std::sin(angle1) * radius,
                                      .u = 0.5F,
                                      .v = 0.5F,
                                      .rgba = rgba},
                            }))
      return false;
  }
  return true;
}

bool Canvas2D::line(const float startX, const float startY, const float endX,
                    const float endY, const float width,
                    const std::uint32_t rgba, const BlendMode blend,
                    const ScissorRect scissor) {
  const float deltaX = endX - startX;
  const float deltaY = endY - startY;
  const float length = std::hypot(deltaX, deltaY);
  if (length <= 0.0F || width <= 0.0F)
    return false;
  return imageTransformed(whiteTexture_, (startX + endX) * 0.5F,
                          (startY + endY) * 0.5F, length, width, 0.5F, 0.5F,
                          std::atan2(deltaY, deltaX), {}, rgba, blend, scissor);
}

bool Canvas2D::circleOutline(const float centerX, const float centerY,
                             const float radius, const float width,
                             const std::uint32_t rgba, const int segments,
                             const BlendMode blend, const ScissorRect scissor) {
  if (radius <= 0.0F || width <= 0.0F || segments < 3 || segments > 512)
    return false;
  for (int index = 0; index < segments; ++index) {
    const float angle0 = 2.0F * std::numbers::pi_v<float> * index / segments;
    const float angle1 =
        2.0F * std::numbers::pi_v<float> * (index + 1) / segments;
    if (!line(centerX + std::cos(angle0) * radius,
              centerY + std::sin(angle0) * radius,
              centerX + std::cos(angle1) * radius,
              centerY + std::sin(angle1) * radius, width, rgba, blend, scissor))
      return false;
  }
  return true;
}

bool Canvas2D::flush(std::string &error) {
  const VertexLayout layout = quadVertexLayout();
  const auto vertices = std::span<const QuadVertex>(batch_.vertices());
  const auto indices = std::span<const std::uint16_t>(batch_.indices());
  bool success = true;
  for (const QuadDrawRange &range : batch_.draws()) {
    const auto drawVertices =
        vertices.subspan(range.firstVertex, range.vertexCount);
    const auto drawIndices =
        indices.subspan(range.firstIndex, range.indexCount);
    const auto uniformSet = uniformSets_.find(range.key.uniformSet);
    const std::span<const DrawUniformValue> uniforms =
        uniformSet == uniformSets_.end() ? std::span<const DrawUniformValue>{}
                                         : uniformSet->second;
    const ScissorRect scissor = canvasScissorForView(
        range.key.scissor, scissorOffsetX_, scissorOffsetY_);
    success = commands_.submit(
                  TransientDraw{
                      .viewId = viewId_,
                      .vertices = std::as_bytes(drawVertices),
                      .vertexLayout = layout,
                      .indices = drawIndices,
                      .program = range.key.program,
                      .texture = range.key.texture,
                      .sampler = sampler_,
                      .blend = range.key.blend,
                      .state = {.blend = range.key.blend},
                      .scissor = scissor,
                      .uniforms = uniforms,
                  },
                  error) &&
              success;
    if (!success)
      break;
    ++statistics_.drawCalls;
    statistics_.vertices += range.vertexCount;
    statistics_.indices += range.indexCount;
    statistics_.triangles += range.indexCount / 3U;
  }
  statistics_.quads = static_cast<std::uint32_t>(batch_.quadCount());
  batch_.clear();
  uniformSets_.clear();
  return success;
}

void Canvas2D::setUniformSet(const std::uint32_t id,
                             const std::span<const DrawUniformValue> uniforms) {
  if (id != 0)
    uniformSets_.insert_or_assign(id, uniforms);
}

bool Canvas2D::add(const TextureHandle texture, const Rect2D &destination,
                   const TextureRegion2D &source, const std::uint32_t rgba,
                   const BlendMode blend, const ScissorRect scissor,
                   ProgramHandle program, const std::uint32_t uniformSet) {
  if (!program)
    program = program_;
  if (!texture || !program || !positive(destination) || source.u1 < source.u0 ||
      source.v1 < source.v0)
    return false;
  return batch_.add(
      QuadBatchKey{
          .texture = texture,
          .program = program,
          .uniformSet = uniformSet,
          .blend = blend,
          .scissor = scissor,
      },
      Quad2D{
          .left = destination.x,
          .top = destination.y,
          .right = destination.x + destination.width,
          .bottom = destination.y + destination.height,
          .u0 = source.u0,
          .v0 = source.v0,
          .u1 = source.u1,
          .v1 = source.v1,
          .rgba = rgba,
      });
}

} // namespace demi::runtime::render
