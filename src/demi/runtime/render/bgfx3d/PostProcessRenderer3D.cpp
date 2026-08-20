#include "demi/runtime/render/bgfx3d/PostProcessRenderer3D.h"

#include "demi/runtime/render/backend/QuadBatch.h"

#include <algorithm>
#include <array>
#include <cmath>
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

} // namespace

PostProcessRenderer3D::PostProcessRenderer3D(GpuResources &resources,
                                             RenderCommands &commands)
    : resources_(resources), commands_(commands) {}

PostProcessRenderer3D::~PostProcessRenderer3D() { shutdown(); }

bool PostProcessRenderer3D::initialize(std::string &error) {
  if (program_)
    return true;
  program_ =
      resources_.createBuiltinProgram(BuiltinProgram::PostProcess2D, error);
  sampler_ = resources_.createSampler("s_postColor", error);
  colorAdjustUniform_ =
      resources_.createUniform("u_colorAdjust", UniformType::Vec4, 1, error);
  tintUniform_ =
      resources_.createUniform("u_tint", UniformType::Vec4, 1, error);
  bloomFadeUniform_ =
      resources_.createUniform("u_bloomFade", UniformType::Vec4, 1, error);
  fadeColorUniform_ =
      resources_.createUniform("u_fadeColor", UniformType::Vec4, 1, error);
  if (!program_ || !sampler_ || !colorAdjustUniform_ || !tintUniform_ ||
      !bloomFadeUniform_ || !fadeColorUniform_) {
    shutdown();
    return false;
  }
  return true;
}

void PostProcessRenderer3D::destroyScratchTarget() {
  if (scratch_.frameBuffer)
    resources_.destroy(scratch_.frameBuffer);
  if (scratch_.depth)
    resources_.destroy(scratch_.depth);
  if (scratch_.color)
    resources_.destroy(scratch_.color);
  scratch_ = {};
  scratchWidth_ = 0;
  scratchHeight_ = 0;
}

void PostProcessRenderer3D::shutdown() {
  destroyScratchTarget();
  for (const UniformHandle uniform : {colorAdjustUniform_, tintUniform_,
                                      bloomFadeUniform_, fadeColorUniform_})
    if (uniform)
      resources_.destroy(uniform);
  if (sampler_)
    resources_.destroy(sampler_);
  if (program_)
    resources_.destroy(program_);
  colorAdjustUniform_ = {};
  tintUniform_ = {};
  bloomFadeUniform_ = {};
  fadeColorUniform_ = {};
  sampler_ = {};
  program_ = {};
}

const RenderTargetHandles &PostProcessRenderer3D::scratchTarget(
    const std::uint16_t width, const std::uint16_t height, std::string &error) {
  if (width == 0 || height == 0) {
    error = "Post-process target dimensions must be positive.";
    destroyScratchTarget();
    return scratch_;
  }
  if (scratch_.frameBuffer && scratchWidth_ == width &&
      scratchHeight_ == height)
    return scratch_;
  destroyScratchTarget();
  scratch_ =
      resources_.createRenderTarget({.width = width,
                                     .height = height,
                                     .colorFormat = TextureFormat::RGBA8,
                                     .depth = true,
                                     .debugName = "3D post-process scratch"},
                                    error);
  if (scratch_.frameBuffer && scratch_.color) {
    scratchWidth_ = width;
    scratchHeight_ = height;
  } else {
    destroyScratchTarget();
  }
  return scratch_;
}

bool PostProcessRenderer3D::present(const BgfxCameraFrame3D &frame,
                                    const TextureHandle source,
                                    const FrameBufferHandle destination,
                                    std::string &error) {
  if (!program_ || !sampler_ || !source) {
    error = "Post-process rendering requires initialized GPU resources.";
    return false;
  }
  if (!commands_.configureView2D(
          {.id = static_cast<std::uint16_t>(frame.viewId + 2U),
           .x = destination ? std::uint16_t{0} : frame.viewportX,
           .y = destination ? std::uint16_t{0} : frame.viewportY,
           .width = std::max<std::uint16_t>(frame.viewportWidth, 1),
           .height = std::max<std::uint16_t>(frame.viewportHeight, 1),
           .clear = false,
           .frameBuffer = destination},
          error))
    return false;

  const float width = static_cast<float>(frame.viewportWidth);
  const float height = static_cast<float>(frame.viewportHeight);
  constexpr std::uint32_t White = 0xffffffffU;
  const std::array<QuadVertex, 4> vertices{{
      {.x = 0.0F, .y = 0.0F, .u = 0.0F, .v = 0.0F, .rgba = White},
      {.x = width, .y = 0.0F, .u = 1.0F, .v = 0.0F, .rgba = White},
      {.x = width, .y = height, .u = 1.0F, .v = 1.0F, .rgba = White},
      {.x = 0.0F, .y = height, .u = 0.0F, .v = 1.0F, .rgba = White},
  }};
  constexpr std::array<std::uint16_t, 6> Indices{0, 1, 2, 0, 2, 3};
  const PostProcessStackComponent effect =
      frame.postProcess.value_or(PostProcessStackComponent{});
  const std::array<float, 4> colorAdjust{effect.exposure, effect.contrast,
                                         effect.saturation, effect.vignette};
  const std::array<float, 4> tint{effect.tint.r, effect.tint.g, effect.tint.b,
                                  effect.tint.a};
  const std::array<float, 4> bloomFade{effect.bloom, effect.bloomThreshold,
                                       effect.fade, 0.0F};
  const std::array<float, 4> fadeColor{effect.fadeColor.r, effect.fadeColor.g,
                                       effect.fadeColor.b, effect.fadeColor.a};
  const std::array<DrawUniformValue, 4> uniforms{{
      {.handle = colorAdjustUniform_, .values = colorAdjust},
      {.handle = tintUniform_, .values = tint},
      {.handle = bloomFadeUniform_, .values = bloomFade},
      {.handle = fadeColorUniform_, .values = fadeColor},
  }};
  return commands_.submit(
      {.viewId = static_cast<std::uint16_t>(frame.viewId + 2U),
       .vertices = std::as_bytes(std::span(vertices)),
       .vertexLayout = quadVertexLayout(),
       .indices = Indices,
       .program = program_,
       .texture = source,
       .sampler = sampler_,
       .blend = BlendMode::Opaque,
       .state = {.blend = BlendMode::Opaque,
                 .depthTest = DepthTest::Disabled,
                 .writeDepth = false},
       .scissor = {},
       .uniforms = uniforms},
      error);
}

bool hasPostProcessEffects(
    const std::optional<PostProcessStackComponent> &postProcess) {
  if (!postProcess)
    return false;
  const PostProcessStackComponent &effect = *postProcess;
  return std::abs(effect.exposure) > 0.0001F ||
         std::abs(effect.contrast - 1.0F) > 0.0001F ||
         std::abs(effect.saturation - 1.0F) > 0.0001F ||
         std::abs(effect.tint.r - 1.0F) > 0.0001F ||
         std::abs(effect.tint.g - 1.0F) > 0.0001F ||
         std::abs(effect.tint.b - 1.0F) > 0.0001F ||
         effect.vignette > 0.0001F || effect.bloom > 0.0001F ||
         effect.fade > 0.0001F;
}

} // namespace demi::runtime::render
